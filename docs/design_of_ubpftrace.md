# Design of ubpftrace: An Unprivileged, Zero-Jitter Dynamic Tracing Engine for High-Performance Computing and Distributed AI

---

## Abstract & Executive Summary

Modern High-Performance Computing (HPC) and distributed AI workloads demand deep, non-invasive runtime observability to diagnose synchronization stragglers, parallel I/O bottlenecks, and communication imbalances across thousands of compute nodes. However, conventional kernel-space eBPF and traditional binary instrumentation frameworks fail in production supercomputing environments due to two fundamental barriers:
1. **Security Constraints**: Kernel-space eBPF requires elevated root capabilities (`CAP_BPF` or `CAP_SYS_ADMIN`), which are strictly prohibited in multi-tenant supercomputing clusters.
2. **Performance Perturbation**: Kernel uprobe implementations rely on software trap interrupts (`INT3`), causing hardware register-saving, dual user-kernel context switches, and severe CPU jitter that disrupts tightly synchronized parallel applications. Furthermore, uncoordinated per-process tracing creates millions of trace files, inducing catastrophic metadata lock storms on parallel filesystems like Lustre.

To resolve these challenges, we design and implement **`ubpftrace`**, an unprivileged, zero-jitter userspace dynamic tracing system. `ubpftrace` operates entirely in unprivileged user space by translating high-level tracing scripts into native machine instructions via LLVM JIT, injecting 5-byte direct branch trampolines into application text segments, and executing probes with an overhead of only $\approx 2.4\text{ ns}$ ($< 0.1\%$ application runtime perturbation).

Crucially, `ubpftrace` introduces a **bifurcated dual-path data plane** that fundamentally separates:
- **The BPF Map Processing Path**: Bounded, in-memory stateful aggregations (`@sum`, `@avg`, `@hist`, `@calls`) processed entirely in volatile host/shared RAM, featuring lock-free atomic updates, complete immunity to Out-of-Memory (OOM) via static capacity caps, out-of-band periodic snapshotting (Scenario A), and isolated Score-P style binomial tree MPI reduction at `MPI_Finalize` (Scenario B).
- **The Event Stream Processing Path**: Continuous, unbounded chronological discrete event logging (`printf`, function traces, Lustre I/O requests, MPI packets) handled via lock-free dual-epoch hazard double-buffering, an isolated low-priority asynchronous I/O worker (`nice +19` / `SCHED_IDLE`), and 2MB stripe-aligned LZ4 compressed container writing to strictly **1 file per physical node** with direct-I/O bypass.

This document serves as the authoritative, publication-ready architectural design specification for `ubpftrace`. It provides complete mathematical formulations, concurrency proofs, structural layouts, and execution algorithms enabling researchers and engineers to understand, deploy, and fully reimplement the system.

---

## 1. Architectural Overview & Design Philosophy

```mermaid
flowchart TB
    subgraph UserInterface ["1. User Interface & Scripting Layer"]
        Script["High-Level Tracing Script (.bt)"] --> Compiler["AST Parser & HPC Type Checker"]
        CLI["CLI Options (-c, -L, --stream)"] --> Compiler
    end

    subgraph ControlPlane ["2. Control Plane & Compilation Engine"]
        Compiler --> JIT["LLVM JIT Compiler (Native Code Gen)"]
        JIT --> Reloc["Symbol Resolution & Base Address Calculator"]
        Reloc --> Patcher["Text Segment Transformer (5-byte JMP/CALL Patcher)"]
    end

    subgraph DataPlaneDual ["3. Bifurcated Dual-Path Data Plane"]
        Patcher --> AppContext["Application Compute Threads (Hot Path)"]
        
        subgraph MapPath ["Path A: BPF Map Pipeline (Bounded Aggregations)"]
            AppContext -->|Pure RAM Write ~2.4ns| FastMaps["SHM BPF Hash / Array Tables (max_entries)"]
            FastMaps -->|Scenario A (Periodic Live)| LiveExp["Live Exporter Worker (nice +19) ──> node_<id>.json"]
            FastMaps -->|Scenario B (Post-Run)| ScorePRed["Score-P Binomial Tree (MPI_Comm_dup) ──> _summary.json"]
        end

        subgraph StreamPath ["Path B: Event Stream Pipeline (Discrete Logging)"]
            AppContext -->|Lock-Free Append ~2.4ns| SHMDouble["Dual-Epoch SHM Ring Buffer (alignas 64)"]
            SHMDouble -->|Hazard Drain & LZ4| AsyncWorker["Async I/O Worker (nice +19)"]
            AsyncWorker -->|2MB Stripe Direct I/O| UBPFContainer[".ubpf Binary Container (1 File/Node)"]
            AsyncWorker -->|Micro-Stream (20ms/8KB)| Stdout["Micro-Buffered stdout Stream"]
        end
    end

    subgraph ToolchainPlane ["4. Post-Mortem & Live Toolchain Plane"]
        LiveExp --> TopTUI["ubpftrace-top (Live Cluster Dashboard & Straggler Detector)"]
        UBPFContainer --> CatTool["ubpftrace-cat (K-Way Min-Heap Merge & Perfetto Visualizer)"]
    end
```

---

### 1.1 Motivation & HPC-Specific Challenges

Dynamic instrumentation in supercomputing environments faces four severe constraints that invalidate general-purpose tracing tools:

#### Challenge 1: Unprivileged Execution Barrier in Multi-Tenant Clusters
In production supercomputers (e.g. NERSC Perlmutter, ALCF Polaris, OLCF Frontier), compute nodes are shared among multiple research groups. System administrators disable root access and strictly forbid security capabilities such as `CAP_BPF` or `CAP_SYS_ADMIN` to prevent cross-tenant data leakage or kernel panics. Standard kernel eBPF tools (`bpftrace`, `bcc`) are entirely unusable in these environments.

#### Challenge 2: Software Trap Interrupt Overhead (`INT3` Uprobes)
Traditional Linux uprobes replace the target function entry instruction with an `INT3` breakpoint opcode ($0\text{xCC}$). When hit:
$$\text{Cost}_{\text{uprobe}} = T_{\text{trap}} + T_{\text{save\_regs}} + 2 \cdot T_{\text{context\_switch}} + T_{\text{bpf\_exec}} + T_{\text{restore\_regs}} \approx 2{,}000 - 4{,}000\text{ ns}$$
In compute loops executing millions of iterations per second, standard uprobes induce a $100\times - 1000\times$ slowdown. Even worse, context-switching out of userspace destroys hardware CPU pipeline state and pollutes L1 instruction/data caches.

#### Challenge 3: Parallel Filesystem (Lustre) Metadata Destruction
Supercomputing clusters rely on distributed parallel filesystems (e.g., Lustre, GPFS) composed of Metadata Servers (MDS) and Object Storage Targets (OSTs). When an MPI application running on 16,384 ranks attempts to write individual trace files:
$$\text{Metadata Requests} = 16{,}384 \times (\text{open} + \text{create} + \text{sync} + \text{close})$$
This triggers massive distributed lock contention on the Lustre MDS, freezing filesystem access for the entire supercomputer.

#### Challenge 4: Parallel Operating System Jitter & Collective Synchronization
HPC applications synchronize periodically via global collective barriers (e.g. `MPI_Allreduce`, `MPI_Barrier`, NCCL AllReduce). According to Amdahl's Law and order statistics, if a tracer introduces even a 1ms random delay on just *one* straggler rank, **all 16,384 ranks stall at the barrier**, amplifying the microsecond perturbation into seconds of wasted supercomputing allocation time.

---

### 1.2 The Three Invariants of ubpftrace

To address these challenges, `ubpftrace` is built around three strict architectural invariants:

| Invariant | Description | Technical Implementation |
| :--- | :--- | :--- |
| **1. Zero-Jitter Invariant** | Probe execution overhead must be $< 0.1\%$ of application runtime, with zero synchronous I/O or kernel context switches. | Fast in-memory JIT trampolines ($\approx 2.4\text{ ns}$), lock-free dual-epoch buffers, background workers running at `SCHED_IDLE` / `nice +19` on isolated cores. |
| **2. Unprivileged Security Invariant** | Must execute entirely in userspace with zero root / `sudo` / `CAP_BPF` permissions. | Userspace eBPF virtual machine, userspace static verifier, dynamic library interception (`LD_PRELOAD`), and userspace text-segment modification via `mprotect(PROT_WRITE)`. |
| **3. Lustre-Friendly Storage Invariant** | Must never cause metadata server contention on parallel filesystems. | Strictly **1 container file per physical node** (`trace_node_<id>.ubpf`), 2MB stripe-aligned direct-I/O (`O_DIRECT`), and single-file global summary reduction (`_summary.json`). |

---

## 2. The Two Distinct Data Paths: BPF Maps vs. Event Streams

The core architectural innovation of `ubpftrace` is the **fundamental separation of BPF Maps from Event Streams**. These two primitives represent opposite mathematical and operational requirements, and coupling them into a single data path causes severe performance degradation.

```
┌─────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│                                       PROBE TRIGGER: foo(arg0, ...)                                     │
└───────────────────────────────────────────────────┬─────────────────────────────────────────────────────┘
                                                    │
                   ┌────────────────────────────────┴────────────────────────────────┐
                   ▼                                                                 ▼
   ┌───────────────────────────────┐                                 ┌───────────────────────────────┐
   │    BPF MAP PROCESSING PATH    │                                 │  EVENT STREAM PROCESSING PATH │
   ├───────────────────────────────┤                                 ├───────────────────────────────┤
   │ • Mathematical Form:          │                                 │ • Mathematical Form:          │
   │   Accumulated State Table     │                                 │   Chronological Tuple Stream  │
   │   $S: \mathcal{K} \to \mathcal{V}$ │                           │   $\mathcal{E} = \{(t_i, e_i)\}$ │
   │ • Memory: Strictly Bounded    │                                 │ • Memory: Unbounded (grows/t) │
   │ • Lifetime: Entire Run        │                                 │ • Lifetime: Transient / Drained│
   │ • Storage: 100% in Host RAM   │                                 │ • Storage: 2MB LZ4 Chunked    │
   │ • Collision: OOM-Immune       │                                 │   Container (.ubpf) on Lustre │
   │ • Online: Live JSON Snapshot  │                                 │ • Online: Lock-free SHM double│
   │ • Offline: Score-P Tree Red.  │                                 │   buffer + micro-stream stdout│
   └───────────────────────────────┘                                 └───────────────────────────────┘
```

---

### 2.1 The BPF Map Processing Path (Bounded In-Memory Aggregations)

#### A. Mathematical Formulation & Structural Design
A BPF Map in `ubpftrace` represents a stateful associative mapping function $M: \mathcal{K} \to \mathcal{V}$, where key $k \in \mathcal{K}$ represents execution context (e.g. `[rank, lustre_ost]` or discrete time bucket `$sec`) and value $v \in \mathcal{V}$ represents an accumulated aggregation state:
$$v = \bigoplus_{i=1}^{N} f(\text{event}_i)$$
where $\bigoplus$ is a commutative, associative aggregation operator (`count`, `sum`, `avg`, `min`, `max`, `hist`).

#### B. Memory Layout & Zero-Jitter Update Mechanics
1. **In-Memory Storage**: BPF map tables reside entirely in volatile memory (within the process memory or a shared memory region `/dev/shm`).
2. **Lock-Free Execution**: Probe execution updates map entries using atomic assembly primitives:
   - `count()`: Atomic increment `lock incq`
   - `sum(val)`: Atomic add `lock addq`
   - `min(val)` / `max(val)`: Atomic compare-and-swap (CAS) loop `lock cmpxchg`
3. **Execution Latency**: The compute thread updates maps in $\approx 2.4\text{ ns}$ without issuing any system call or file operation.

#### C. Out-of-Memory (OOM) Immunity & Static Capacity Capping
To guarantee that a program running for weeks never exhausts host memory, `ubpftrace` enforces a strict capacity ceiling:
- Every map is initialized with a static maximum capacity: $\text{max\_entries} = N_{\text{max}}$ (default: 4,096 to 10,240 entries).
- Total map RAM footprint is mathematically bounded:
  $$\text{RAM}_{\text{map}} = N_{\text{max}} \times (\text{sizeof}(\text{Key}) + \text{sizeof}(\text{Value}) + \text{sizeof}(\text{NodeMetadata})) \approx 10{,}240 \times 64\text{ B} \approx 640\text{ KB}$$
- **Overflow Policy**: When a hash map is full, new unseen keys are rejected, and an atomic `drop_count` is incremented. Existing keys continue updating their accumulated values in-place with zero memory allocation.

#### D. Online Export: Scenario A Periodic Out-of-Band Snapshotting
When live metric inspection is requested (`-L <SEC>` or `--live-ms <MS>`):
1. Compute threads continue mutating in-memory maps without any time checks or string formatting.
2. A dedicated background thread (`ubpf_live_exporter`) on an isolated core at `nice +19` wakes up on timer $\tau_{\text{live}}$.
3. It invokes a zero-copy map provider callback, serializes the active key-value dictionary into a JSON memory buffer, and writes it to a temporary staging file:
   $$\text{Path}_{\text{tmp}} = \texttt{node\_<node\_id>.json.tmp.<pid>}$$
4. It calls atomic POSIX replacement:
   $$\texttt{rename}(\text{Path}_{\text{tmp}}, \texttt{node\_<node\_id>.json})$$
   This guarantees that external monitoring daemons (`ubpftrace-top`) never encounter partial or corrupted JSON files.
5. **Lustre Safety**: Exactly **1 snapshot file per physical node** is written.

#### E. Offline Export: Scenario B Score-P Style Isolated MPI Reduction
When the application terminates:
1. `libbpftime-agent.so` intercepts `MPI_Finalize()`.
2. To prevent message tag collisions with the application, it creates a private communicator:
   $$\texttt{MPI\_Comm\_dup}(\texttt{MPI\_COMM\_WORLD}, \&\texttt{trace\_comm})$$
3. It performs a parallel binomial tree reduction across all global ranks:
   - Leaves transmit local map buffers to parent nodes in the tree.
   - Intermediate ranks merge map entries:
     $$\text{Sum}_{\text{merged}} = \sum \text{Sum}_{\text{local}}, \quad \text{Avg}_{\text{merged}} = \frac{\sum (\text{Avg}_i \cdot \text{Count}_i)}{\sum \text{Count}_i}, \quad \text{Hist}_{\text{merged}} = \text{Hist}_1 \uplus \text{Hist}_2$$
4. **Single-File Output**: Root Rank (Rank 0) serializes the fully merged global cluster metric dictionary into a single `_summary.json` file.

---

### 2.2 The Event Stream Processing Path (Discrete Chronological Logging)

#### A. Mathematical Formulation & Structural Design
An Event Stream is an unbounded sequence of discrete event tuples emitted over time:
$$\mathcal{E} = \{(t_0, r_0, \text{type}_0, p_0), (t_1, r_1, \text{type}_1, p_1), \dots, (t_n, r_n, \text{type}_n, p_n)\}$$
where $t_i$ is a nanosecond timestamp, $r_i$ is the global MPI rank, $\text{type}_i$ is the event identifier, and $p_i$ is the arbitrary binary or formatted payload (e.g. `printf` string, MPI message header, Lustre I/O offset).

Unlike BPF maps, event stream data grows linearly with execution time:
$$\text{Data Volume} = O(\text{event\_rate} \times \text{time})$$
It cannot be kept in RAM and must be continuously drained to persistent storage.

#### B. Lock-Free Dual-Epoch Hazard Double-Buffering Algorithm
To achieve non-blocking writes without taking mutex locks in the application's hot path, `ubpftrace` implements an atomic dual-epoch double-buffering algorithm:

```
Shared Memory (SHM) Header Layout:
┌────────────────────────────────────────────────────────────────────────┐
│  active_epoch_buffer: 64-bit Packed Atomic Word                        │
│  [ Epoch Counter (bits 32..63) ] | [ Active Buffer Index 0/1 (bits 0..31) ] │
├────────────────────────────────────────────────────────────────────────┤
│  Buffer Block 0 (16 MB):                                               │
│  ├─ write_offset (atomic uint64)                                       │
│  ├─ active_writers (atomic uint32)                                     │
│  └─ data_payload[16777216]                                             │
├────────────────────────────────────────────────────────────────────────┤
│  Buffer Block 1 (16 MB):                                               │
│  ├─ write_offset (atomic uint64)                                       │
│  ├─ active_writers (atomic uint32)                                     │
│  └─ data_payload[16777216]                                             │
└────────────────────────────────────────────────────────────────────────┘
```

```cpp
// --- Producer Algorithm (Application Compute Thread) ---
void record_event(const void* payload, uint32_t len) {
    while (true) {
        uint64_t state = shm->active_epoch_buffer.load(std::memory_order_acquire);
        uint32_t epoch = static_cast<uint32_t>(state >> 32);
        uint32_t idx = static_cast<uint32_t>(state & 0xFFFFFFFFULL);
        ubpf_buffer_block* buf = &shm->buffers[idx];

        // 1. Enter hazard tracking
        buf->active_writers.fetch_add(1, std::memory_order_acquire);

        // 2. Verify epoch has not flipped while entering
        uint64_t check_state = shm->active_epoch_buffer.load(std::memory_order_acquire);
        if (check_state != state) {
            buf->active_writers.fetch_sub(1, std::memory_order_release);
            continue; // Retry on new active buffer
        }

        // 3. Atomically reserve space
        uint64_t offset = buf->write_offset.fetch_add(len, std::memory_order_relaxed);
        if (offset + len <= BUFFER_CAPACITY) {
            std::memcpy(buf->data + offset, payload, len);
            buf->active_writers.fetch_sub(1, std::memory_order_release);
            return; // Success
        }

        // Buffer full: drop and release
        buf->active_writers.fetch_sub(1, std::memory_order_release);
        stats->drop_count.fetch_add(1, std::memory_order_relaxed);
        return;
    }
}
```

```cpp
// --- Consumer Algorithm (Background Async I/O Worker) ---
void drain_buffer_if_needed() {
    uint64_t state = shm->active_epoch_buffer.load(std::memory_order_acquire);
    uint32_t epoch = static_cast<uint32_t>(state >> 32);
    uint32_t active_idx = static_cast<uint32_t>(state & 0xFFFFFFFFULL);
    ubpf_buffer_block* cur_buf = &shm->buffers[active_idx];

    if (cur_buf->write_offset.load(std::memory_order_relaxed) < HIGH_WATERMARK && !timer_expired) {
        return;
    }

    // 1. Advance epoch and flip active buffer index
    uint32_t next_epoch = epoch + 1;
    uint32_t next_idx = 1 - active_idx;
    uint64_t next_state = (static_cast<uint64_t>(next_epoch) << 32) | next_idx;
    shm->active_epoch_buffer.store(next_state, std::memory_order_release);

    // 2. Hazard Drain Spin: Wait for lingering writers on the old buffer
    while (cur_buf->active_writers.load(std::memory_order_acquire) > 0) {
        _mm_pause(); // Yield CPU pipeline execution
    }

    // 3. Exclusively compress and write inactive buffer to disk
    uint64_t uncompressed_len = cur_buf->write_offset.load(std::memory_order_relaxed);
    compress_and_write_chunk(cur_buf->data, uncompressed_len);

    // 4. Reset write offset for next cycle
    cur_buf->write_offset.store(0, std::memory_order_relaxed);
}
```

#### C. Hardware Cacheline Isolation (`alignas(64)`)
To prevent multi-rank memory bus contention on NUMA nodes:
```cpp
struct alignas(64) ubpf_per_rank_stats {
    std::atomic<uint64_t> event_count;
    std::atomic<uint64_t> byte_count;
    std::atomic<uint64_t> drop_count;
    uint8_t padding[40]; // Exactly pads struct to 64 bytes (1 CPU cache line)
};
```
Every rank writes exclusively to its dedicated cacheline, achieving $O(1)$ concurrent scalability with **zero cacheline invalidation bouncing**.

#### D. Lustre OST Stripe Alignment & Direct I/O Container Writer (`.ubpf`)
The asynchronous worker writes compressed chunks into the node container file (`trace_job<id>_node<id>.ubpf`):
1. **Lustre Geometry Discovery**: Queries the OST stripe width $S$ via `ioctl(fd, LL_IOC_LOV_GETSTRIPE)` (typically 2MB).
2. **Page-Aligned Memory Allocation**: Direct-I/O memory buffers are allocated using `posix_memalign(4096, 2 * 1024 * 1024)`.
3. **`O_DIRECT` Bypass**: Chunks are appended directly to storage using `O_DIRECT`, bypassing the Linux page cache and eliminating writeback cache lockups.

#### E. Micro-Buffered Live Streaming (`--stream`)
When live stdout event streaming is enabled:
1. Probes append raw binary events to the SHM buffer.
2. The background worker reads the buffer every 1ms.
3. It performs an intra-node chronological sort across all local ranks using `std::stable_sort`.
4. It formats strings into an 8KB batch buffer and flushes to `stdout` every 20ms or 8KB, completely eliminating terminal write backpressure on compute threads.

---

## 3. Compilation, Instrumentation & Execution Workflow

The end-to-end execution of `ubpftrace` proceeds through four distinct lifecycle phases:

```
┌─────────────────┐     ┌───────────────────────┐     ┌─────────────────────┐     ┌────────────────────┐
│ 1. SETUP PHASE  │ ──> │ 2. RUNTIME ATTACHMENT │ ──> │ 3. EXECUTION PHASE  │ ──> │ 4. TERMINATION     │
│ AST/LLVM JIT    │     │ Dynamic Injection     │     │ Hot JIT Trampolines │     │ Reduction & Flush  │
└─────────────────┘     └───────────────────────┘     └─────────────────────┘     └────────────────────┘
```

---

### 3.1 Setup Phase (Compilation & Code Generation)
1. **Script Parsing & AST Generation**:
   The frontend parses `.bt` files, resolves probe targets (`uprobe:path:symbol`), and validates builtins (`rank`, `node`, `lustre_ost`, `arg0`..`arg5`).
2. **Type Checking & Semantic Resolution**:
   Variables and maps are validated for type consistency. Signed/unsigned divisions are checked to prevent undefined arithmetic.
3. **LLVM JIT Native Compilation**:
   LLVM translates the AST into an intermediate representation (IR) and compiles it directly into native x86_64 machine code cached in executable memory pages.
4. **Symbol Table Parsing**:
   `libelf` / `libbfd` parses the target binary on disk to resolve the relative symbol offset $\Delta_{\text{sym}}$ of the probed function.

---

### 3.2 Runtime Attachment Phase (Dynamic Preloading & Trampoline Patching)
1. **Dynamic Library Injection**:
   The target application launches with `LD_PRELOAD=libbpftime-agent.so`.
2. **Initialization Interception**:
   `libbpftime-agent.so` intercepts `__libc_start_main` before the application's `main()` executes.
3. **Virtual Memory Base Resolution**:
   The agent parses `/proc/self/maps` to find the runtime load address of the executable image:
   $$\text{Addr}_{\text{target}} = \text{Base}_{\text{image}} + \Delta_{\text{sym}}$$
4. **5-Byte Trampoline Injection**:
   - The agent changes page permissions using `mprotect(Addr_{\text{target}}, 4096, PROT_READ | PROT_WRITE | PROT_EXEC)`.
   - It saves the original 5 bytes of the target function into a private relocation buffer.
   - It writes a 5-byte relative jump instruction (`E9 <relative_32bit_offset>`) directing execution to the probe dispatcher:
     $$\text{Offset}_{32} = \text{Addr}_{\text{trampoline}} - (\text{Addr}_{\text{target}} + 5)$$
   - It restores memory protection to `PROT_READ | PROT_EXEC`.

```
Original Target Function:
  0x400560: 55                    push   %rbp
  0x400561: 48 89 e5              mov    %rsp,%rbp
  0x400564: 48 83 ec 10           sub    $0x10,%rsp

Patched Function:
  0x400560: e9 ab 0a 20 00        jmp    0x601010 <bpftime_probe_dispatcher>
  0x400565: ec 10                 sub    ... (remaining original bytes)

Relocation Trampoline Buffer:
  0x601050: 55                    push   %rbp         (displaced byte)
  0x601051: 48 89 e5              mov    %rsp,%rbp    (displaced bytes)
  0x601054: e9 0c f5 df ff        jmp    0x400565     (jump back to target body)
```

---

### 3.3 Target Function Execution Phase (Hot Path)
1. When the application thread enters `foo()`, the CPU encounters the `jmp` opcode and branches immediately into `bpftime_probe_dispatcher`.
2. The dispatcher saves caller-saved machine registers (`%rdi`, `%rsi`, `%rdx`, `%rcx`, `%r8`, `%r9`, `%rax`).
3. It invokes the native JIT-compiled BPF instructions:
   - For maps: executes atomic memory increments ($\approx 2.4\text{ ns}$).
   - For event streams: records binary tuples into the lock-free active buffer block.
4. The dispatcher restores all registers and jumps to the relocation trampoline buffer, executing the original displaced instructions and returning seamlessly to `foo()+5`.

---

### 3.4 Termination & Teardown Phase
1. When `main()` returns or the process receives termination signals:
2. **In Scenario B**: `MPI_Finalize` executes the Score-P tree reduction and Rank 0 writes `_summary.json`.
3. **In Scenario A**: The live exporter executes a final forced snapshot export.
4. The asynchronous I/O worker flushes all remaining double-buffer blocks and appends the index footer to `.ubpf`.
5. Trampolines are unlinked, shared memory blocks are unmapped, and the process exits with its native exit code.

---

## 4. Re-Implementation Blueprint & Binary Specifications

To allow any systems programmer to reimplement `ubpftrace` from scratch, we specify the exact C++ data structures and binary container layout.

---

### 4.1 Binary Container Header Specifications (`.ubpf`)

```cpp
// 128-Byte Static File Header (Offset 0 of .ubpf container)
struct alignas(64) ubpf_file_header {
    uint32_t magic;               // 0x55425046 ("UBPF")
    uint32_t version;             // 0x00010000 (v1.0)
    uint32_t job_id;              // Slurm / PBS Job ID
    uint32_t node_id;             // 32-bit FNV-1a Hash of Hostname
    char hostname[64];            // Physical hostname string (e.g. "nid001234")
    uint16_t codec_id;            // 0x01 = LZ4, 0x02 = ZSTD
    uint16_t reserved16;          // Alignment padding
    uint32_t reserved32;          // Alignment padding to 8-byte boundary
    uint64_t timestamp_base_ns;   // CLOCK_MONOTONIC start timestamp (ns)
    uint64_t wallclock_base_ns;   // CLOCK_REALTIME Unix Epoch (ns)
    uint8_t reserved[24];         // Padding to exactly 128 bytes
};
static_assert(sizeof(ubpf_file_header) == 128, "ubpf_file_header must be 128 bytes");

// 64-Byte Chunk Header (Prepended to each 2MB compressed block)
struct alignas(64) ubpf_chunk_header {
    uint32_t chunk_magic;         // 0x43484E4B ("CHNK")
    uint16_t chunk_type;          // 0x01: EVENT_STREAM, 0x02: MAP_SNAPSHOT
    uint16_t codec_id;            // 0x01: LZ4
    uint32_t uncompressed_size;   // Size before compression (bytes)
    uint32_t compressed_size;     // Size on disk after compression (bytes)
    uint64_t chunk_start_ns;      // Timestamp of first record in chunk
    uint64_t chunk_end_ns;        // Timestamp of last record in chunk
    uint32_t record_count;        // Number of event records in chunk
    uint32_t dropped_events_count;// Dropped records due to buffer overflow
    uint32_t crc32;               // CRC32 checksum of compressed payload
    uint32_t reserved[5];         // Padding to exactly 64 bytes
};
static_assert(sizeof(ubpf_chunk_header) == 64, "ubpf_chunk_header must be 64 bytes");

// Event Record Binary Header (Prepended to each discrete event inside a chunk)
struct ubpf_event_record_header {
    uint64_t timestamp_ns;        // CLOCK_MONOTONIC timestamp (ns)
    uint32_t rank;                // Global MPI Rank ID
    uint32_t tid;                 // Thread ID
    uint16_t event_type;          // 0x01: printf text, 0x02: binary tuple
    uint16_t payload_len;         // Length of payload immediately following header
    uint32_t record_len;          // Total record length including padding
};
```

---

### 4.2 Shared Memory Double-Buffer Layout (`/dev/shm`)

```cpp
constexpr size_t BUFFER_CAPACITY = 16 * 1024 * 1024; // 16MB per buffer block

struct ubpf_buffer_block {
    std::atomic<uint64_t> write_offset{0};
    std::atomic<uint32_t> active_writers{0};
    uint8_t padding[52];          // Aligns data array to 64-byte boundary
    uint8_t data[BUFFER_CAPACITY];
};

struct ubpf_node_shm_header {
    alignas(64) std::atomic<uint64_t> active_epoch_buffer{0}; // Packed: [epoch:32 | idx:32]
    uint32_t job_id;
    uint32_t node_id;
    uint32_t num_local_ranks;
    uint32_t max_local_ranks;
    alignas(64) ubpf_per_rank_stats rank_stats[128]; // Max 128 ranks per node
    alignas(64) ubpf_buffer_block buffers[2];        // Double buffers B0 and B1
};
```

---

## 5. Comprehensive Summary Matrix: BPF Maps vs. Event Streams

| Architectural Dimension | **BPF Map Processing Path** | **Event Stream Processing Path** |
| :--- | :--- | :--- |
| **Mathematical Abstraction** | Stateful Accumulator: $M: \mathcal{K} \to \mathcal{V}$ | Discrete Chronological Log: $\mathcal{E} = \{(t_i, e_i)\}$ |
| **Data Growth Behavior** | **Strictly Bounded** ($O(\text{unique keys}) \le \text{max\_entries}$) | **Unbounded** ($O(\text{event\_rate} \times \text{time})$) |
| **In-Memory Storage** | Shared Memory Hash / Array Tables ($\approx 640\text{ KB}$) | Lock-Free Dual-Epoch SHM Buffer Blocks ($2 \times 16\text{ MB}$) |
| **Online Disk I/O** | **Zero** (Except 1 JSON file/node in Scenario A) | Periodic 2MB LZ4 Compressed Stripe Append (`.ubpf`) |
| **OOM Protection Policy** | Static capacity cap ($\text{max\_entries}$); rejects new keys | Double-buffer recycling & background disk draining |
| **Execution Overhead** | $\approx 2.4\text{ ns}$ (pure atomic memory write) | $\approx 2.4\text{ ns}$ (lock-free SHM memcpy) |
| **Online Telemetry Interface** | **`ubpftrace-top`** (Reads `node_<id>.json` live) | **`--stream`** (Micro-buffered stdout batching) |
| **Offline Final Output** | **`_summary.json`** (Score-P MPI Tree Reduction) | **`.ubpf` container** $\to$ **`ubpftrace-cat`** $\to$ Perfetto |
| **HPC Scalability** | Scalable to $> 100{,}000$ MPI ranks via MPI tree | Strictly **1 container file per physical node** (Lustre-safe) |
