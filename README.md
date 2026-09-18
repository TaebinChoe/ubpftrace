# `ubpftrace`: Userspace eBPF Tracing Engine

<p align="center">
  <b>High-Level Scripting (<code>.bt</code>) &bull; Zero Root Privileges &bull; Zero Context Switches &bull; Nanosecond Overhead</b>
</p>

---

## 📖 Overview

**`ubpftrace`** is a high-performance **userspace dynamic tracing tool** that combines the high-level scripting frontend of **`bpftrace`** with the userspace eBPF JIT execution and inline hooking engine of **`bpftime`**.

Standard kernel-based `bpftrace` requires `root` privileges (`sudo` / `CAP_BPF`) and incurs substantial **context-switching overhead** (via `INT3` breakpoint traps and kernel page table switches) each time a userspace probe (`uprobe`) fires.

**`ubpftrace` solves both problems:**
1. **Zero Root / Sudo Required**: Runs as a standard non-privileged user.
2. **Zero Context Switching**: Hooks user-level code directly in User Mode (Ring 3) via Frida-Gum 5-byte near jumps and executes eBPF bytecode via LLVM JIT (`llvmbpf`), delivering **up to 10x lower latency overhead**.
3. **Full `.bt` Script Compatibility**: Supports standard `bpftrace` syntax, C struct definitions, logarithmic histograms (`hist()`), multi-key maps, and statistical aggregations (`stats()`, `sum()`, `min()`, `max()`).

---

## ⚡ Quick Comparison

| Feature | Standard `bpftrace` | `ubpftrace` |
| :--- | :--- | :--- |
| **Execution Mode** | Kernel Space (Ring 0) | **Pure Userspace (Ring 3)** |
| **Root Privileges** | ❌ **Required (`sudo` / `CAP_BPF`)** | ✅ **None Required (Regular User)** |
| **Uprobe Mechanism** | Kernel `INT3` Trap / Context Switch | **Frida-Gum Inline 5-byte `JMP`** |
| **Execution Engine** | Kernel BPF In-Tree JIT | **LLVM JIT (`llvmbpf`) / uBPF** |
| **Map Storage** | Kernel BPF Maps | **POSIX Shared Memory (`/dev/shm`)** |
| **Scripting Language** | Standard `bpftrace` (`.bt`) | **Standard `bpftrace` (`.bt`)** |
| **Tracefs Dependency** | Required (`/sys/kernel/tracing`) | **None** |

---

## 🛠️ Prerequisites & Dependencies

### Option A: Standard Linux (Ubuntu / Debian)
```bash
sudo apt update
sudo apt install -y \
    build-essential \
    cmake \
    clang \
    llvm-18 \
    llvm-18-dev \
    libclang-18-dev \
    libelf-dev \
    zlib1g-dev \
    libboost-all-dev \
    libspdlog-dev \
    binutils-dev \
    bison \
    flex \
    asciidoctor \
    pahole
```

### Option B: HPC Supercomputers & Conda Environments (NERSC Perlmutter, Cray EX, SUSE/RHEL)
On multi-tenant HPC systems without `sudo`, install dependencies via Conda (e.g. `conda-forge`):

```bash
# 1. Activate your Conda environment
conda activate <your_env>   # e.g., conda activate tchoe_env

# 2. Unset conflicting global include paths (Prevents stdlib.h collision)
unset C_INCLUDE_PATH
unset CPLUS_INCLUDE_PATH

# 3. Install core build dependencies
conda install -y -c conda-forge \
    cmake \
    boost-cpp \
    elfutils \
    binutils \
    zlib \
    bison \
    flex \
    spdlog \
    cereal
```

> **Note on `bcc` & LLVM:** If using `bcc` with LLVM in Conda, ensure `bcc` is built with `-DENABLE_LLVM_SHARED=ON` against Conda's `libLLVM.so` to avoid duplicate command-line option registration.

---

## 📦 Building from Source (Copy & Paste)

### Method 1: Automated 1-Click HPC / Conda Build (Recommended for HPC)

The repository provides an automated build script [`scripts/build_hpc.sh`](scripts/build_hpc.sh) that auto-detects GCC, sanitizes environment variables, auto-compiles `libiberty.a` if needed, resolves Boost headers from Conda, and builds all binaries:

```bash
# Clone the repository
git clone https://github.com/TaebinChoe/ubpftrace.git
cd ubpftrace

# Run 1-click automated build
./scripts/build_hpc.sh
```

---

### Method 2: Manual CMake Build

```bash
# 1. Clone and navigate to repository
git clone https://github.com/TaebinChoe/ubpftrace.git
cd ubpftrace

# 2. Configure (Specify GCC to avoid Cray wrapper conflicts)
unset C_INCLUDE_PATH CPLUS_INCLUDE_PATH
export LD_LIBRARY_PATH="$CONDA_PREFIX/lib:$CONDA_PREFIX/lib64:$LD_LIBRARY_PATH"

cmake -B build -S . \
    -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_TESTS=OFF \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DCMAKE_C_COMPILER=/usr/bin/gcc \
    -DCMAKE_CXX_COMPILER=/usr/bin/g++ \
    -DCMAKE_PREFIX_PATH="$CONDA_PREFIX;$CONDA_PREFIX/lib;$CONDA_PREFIX/lib64"

# 3. Compile
cmake --build build -j$(nproc)
```

All compiled binaries (`bin/ubpftrace`, `bin/libbpftime-agent.so`, `bin/libbpftime-syscall-server.so`) are generated automatically in the `bin/` directory.

Verify the installation:

```bash
./bin/ubpftrace --version
```
*(Outputs `bpftrace v0.27.0` cleanly without requiring `sudo` or `root`)*

---

## 🚀 Quick Start & Getting Started Tutorial

For a complete step-by-step tutorial covering the 5 fundamental usage patterns of `ubpftrace` (Function Tracing, In-Memory BPF Maps, Time-Window Aggregations, Live Cluster Top, and Container Offloading), see:

* 📖 **[Getting Started Documentation Guide](docs/getting_started.md)**
* 💻 **[Getting Started Example Directory (`examples/getting_started/`)](examples/getting_started/)**

Run all 5 getting started scenarios with a single command:
```bash
./examples/getting_started/run.sh
```

---

## 🧪 Interactive Examples & Test Suite

The repository includes ready-to-run test cases under the [`examples/`](examples) directory.

### Run All Integration Tests
Execute the automated test suite verifying all subsystems (compiles sample apps automatically):

```bash
./run_tests.sh
```

---

### Example 1: User-Defined Function Tracing (`examples/calc.bt`)

Trace custom functions in user binaries with function inputs (`arg0`..`argN`) and return values (`retval`):

#### Script (`examples/calc.bt`):
```bt
uprobe:calc:calculate {
    printf("[ubpftrace] calculate called: a=%d, b=%d\n", arg0, arg1);
    @calls = count();
}

uretprobe:calc:calculate {
    printf("[ubpftrace] calculate returned: retval=%d\n", retval);
}
```

#### Run Command:
```bash
make -C examples/apps
PATH="examples/apps:$PATH" ./bin/ubpftrace -c "examples/apps/calc" ./examples/calc.bt
```

---

### Example 2: Shared Library Shorthand Tracing (`examples/puts.bt`)

Trace standard library functions using aliases (`libc:puts`, `c:puts`) and read string arguments via `str()`:

#### Script (`examples/puts.bt`):
```bt
uprobe:libc:puts {
    printf("[ubpftrace] libc puts() intercepted: %s\n", str(arg0));
    @puts_count = count();
}

END {
    printf("[ubpftrace] Libc tracing finished!\n");
}
```

#### Run Command:
```bash
./bin/ubpftrace -c "examples/apps/puts_app" ./examples/puts.bt
```

---

### Example 3: Memory Allocation Histograms (`examples/malloc.bt`)

Profile dynamic memory allocations with logarithmic power-of-2 distributions (`hist()`):

#### Script (`examples/malloc.bt`):
```bt
uprobe:libc:malloc {
    printf("[ubpftrace] malloc called: size=%d bytes\n", arg0);
    @alloc_bytes = sum(arg0);
    @alloc_count = count();
    @alloc_hist = hist(arg0);
}

uretprobe:libc:malloc {
    printf("[ubpftrace] malloc returned: ptr=0x%lx\n", retval);
}
```

#### Run Command:
```bash
./bin/ubpftrace -c "examples/apps/alloc_app" ./examples/malloc.bt
```

#### Sample Output:
```text
@alloc_bytes: 6903
@alloc_count: 17
@alloc_hist: 
[16, 32)               3 |@@@@@@@@@@@@@@@@@                                   |
[32, 64)               9 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
[64, 128)              1 |@@@@@                                               |
[128, 256)             0 |                                                    |
[256, 512)             1 |@@@@@                                               |
[512, 1K)              0 |                                                    |
[1K, 2K)               2 |@@@@@@@@@@@                                         |
[2K, 4K)               0 |                                                    |
[4K, 8K)               1 |@@@@@                                               |
```

---

### Example 4: C Struct Decoding & Statistical Aggregations (`examples/struct_trace.bt`)

Declare C `struct` definitions at the top of your script, dereference pointers, index multi-key maps, and calculate statistical summaries (`stats()`, `min()`, `max()`, `sum()`):

#### Script (`examples/struct_trace.bt`):
```bt
struct Packet {
    unsigned int magic;
    unsigned int packet_id;
    unsigned long size_bytes;
    char *source_name;
};

uprobe:struct_app:process_packet {
    $pkt = (struct Packet *)arg0;
    printf("[ubpftrace] Packet: id=%u, size=%u, src=%s, magic=0x%x\n",
           $pkt->packet_id, $pkt->size_bytes, str($pkt->source_name), $pkt->magic);
    
    @bytes_total = sum($pkt->size_bytes);
    @min_size = min($pkt->size_bytes);
    @max_size = max($pkt->size_bytes);
    @stats_size = stats($pkt->size_bytes);
    @pkts_by_id[comm, $pkt->packet_id] = count();
}

uretprobe:struct_app:process_packet {
    printf("[ubpftrace] process_packet returned status=%d\n", retval);
}
```

#### Run Command:
```bash
PATH="examples/apps:$PATH" ./bin/ubpftrace -c "examples/apps/struct_app" ./examples/struct_trace.bt
```

#### Sample Output:
```text
@bytes_total: 2560
@max_size: 1024
@min_size: 256
@pkts_by_id[struct_app, 100]: 1
@pkts_by_id[struct_app, 200]: 1
@pkts_by_id[struct_app, 300]: 1
@pkts_by_id[struct_app, 400]: 1
@stats_size: { .count = 4, .average = 640, .total = 2560 }
```

---

### Example 5: Periodic Userspace Timers (`examples/interval.bt`)

Trigger periodic events (`interval:1s`, `i:ms:500`) in pure userspace:

#### Script (`examples/interval.bt`):
```bt
i:ms:500 {
    printf("[ubpftrace timer] Periodic tick! (elapsed: %d ms)\n", elapsed / 1000000);
}

uprobe:libc:puts {
    printf("[ubpftrace uprobe] puts: %s\n", str(arg0));
}
```

#### Run Command:
```bash
./bin/ubpftrace -c "examples/apps/puts_app" ./examples/interval.bt
```

---

### Example 6: HPC MPI Communication & Synchronization Bottleneck Tracking (`examples/mpi_bottleneck.bt`)

Identify communication bottlenecks and load imbalance (stragglers) in high-performance distributed computing workloads by tracing `MPI_Send`, `MPI_Recv`, `MPI_Barrier`, and `MPI_Allreduce`:

#### Script (`examples/mpi_bottleneck.bt`):
```bt
uprobe:mpi:MPI_Send {
    $count = arg1;
    $dest = arg3;
    printf("[ubpftrace MPI] MPI_Send(count=%u elements, dest_rank=%u)\n", $count, $dest);
    @mpi_call_counts["MPI_Send"] = count();
    @send_msg_elements_hist = hist($count);
    @total_send_elements = sum($count);
    @send_volume_by_dest[$dest] = sum($count);
}

uprobe:mpi:MPI_Recv {
    $count = arg1;
    $src = arg3;
    printf("[ubpftrace MPI] MPI_Recv(max_count=%u elements, src_rank=%u)\n", $count, $src);
    @mpi_call_counts["MPI_Recv"] = count();
    @recv_msg_elements_hist = hist($count);
    @total_recv_elements = sum($count);
}

uprobe:mpi:MPI_Barrier {
    @barrier_start[tid] = nsecs;
    @mpi_call_counts["MPI_Barrier"] = count();
}

uretprobe:mpi:MPI_Barrier /@barrier_start[tid]/ {
    $dur_us = (nsecs - @barrier_start[tid]) / 1000;
    printf("[ubpftrace MPI] MPI_Barrier wait time: %u us\n", $dur_us);
    @barrier_latency_us = hist($dur_us);
    @barrier_stats_us = stats($dur_us);
    @total_barrier_time_us = sum($dur_us);
    _ = delete(@barrier_start, tid);
}

uprobe:mpi:MPI_Allreduce {
    @allreduce_start[tid] = nsecs;
    @mpi_call_counts["MPI_Allreduce"] = count();
}

uretprobe:mpi:MPI_Allreduce /@allreduce_start[tid]/ {
    $dur_us = (nsecs - @allreduce_start[tid]) / 1000;
    printf("[ubpftrace MPI] MPI_Allreduce latency: %u us\n", $dur_us);
    @allreduce_latency_us = hist($dur_us);
    @allreduce_stats_us = stats($dur_us);
    _ = delete(@allreduce_start, tid);
}
```

#### Run Command (Multi-Node / Slurm HPC Environment):
```bash
# Option A: Using the 1-Click Multi-Node Launcher (Auto salloc + srun)
./scripts/run_mpi_srun.sh <num_nodes> <num_tasks> <time_limit>
# Example: Trace across 2 nodes with 2 MPI ranks:
./scripts/run_mpi_srun.sh 2 2 00:05:00

# Option B: Running directly with salloc + srun
salloc -N 2 -C cpu -q interactive -t 00:05:00 -- \
    srun -N 2 -n 2 ./bin/ubpftrace -c "examples/apps/hpc_app" ./examples/mpi_bottleneck.bt
```

#### Sample Output (Tracing Across 2 Physical Nodes):
```text
Attached 7 probes
Attached 7 probes
[ubpftrace MPI] MPI_Send(count=1024 elements, dest_rank=1)
[ubpftrace MPI] MPI_Recv(max_count=1024 elements, src_rank=1)
[ubpftrace MPI] MPI_Recv(max_count=1024 elements, src_rank=0)
[ubpftrace MPI] MPI_Send(count=1024 elements, dest_rank=0)
[ubpftrace MPI] MPI_Barrier wait time: 40070 us
[ubpftrace MPI] MPI_Allreduce latency: 81 us
[Rank 0/2] HPC Worker started (node: nid004222).
[Rank 1/2] HPC Worker started (node: nid004223).

@barrier_latency_us: 
[32K, 64K)             3 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
@barrier_stats_us: { .count = 3, .average = 40046, .total = 120140 }

@allreduce_latency_us: 
[16, 32)               1 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
[32, 64)               1 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
[64, 128)              1 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
@allreduce_stats_us: { .count = 3, .average = 44, .total = 133 }

@mpi_call_counts[MPI_Allreduce]: 3
@mpi_call_counts[MPI_Barrier]: 3
@mpi_call_counts[MPI_Recv]: 3
@mpi_call_counts[MPI_Send]: 3
@send_msg_elements_hist: 
[1K, 2K)               3 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
@send_volume_by_dest[0]: 3072
@total_barrier_time_us: 120140
@total_send_elements: 3072
```

---

### Example 7: Lustre Parallel File System Bottleneck Profiling (`examples/lustre_profiler.bt`)

Identify I/O bottlenecks across Lustre Object Storage Targets (OSTs) by dynamically resolving file descriptors and byte offsets to physical OST indices (`obdidx`) in pure userspace:

#### Builtin Helper: `lustre_ost(int fd, uint64_t offset)`
- Issues `ioctl(fd, LL_IOC_LOV_GETSTRIPE, ...)` and caches file stripe metadata per file descriptor.
- Computes target OST index: `stripe_idx = (offset / stripe_size) % stripe_count` -> `lmm_objects[stripe_idx].l_ost_idx`.
- Returns physical target `ost_id` (or -1 for non-Lustre files).

#### Script (`examples/lustre_profiler.bt`):
```bt
uprobe:/lib64/libc.so.6:pwrite64 {
    $fd = arg0;
    $bytes = arg2;
    $offset = arg3;
    $ost = lustre_ost($fd, $offset);
    printf("pwrite64 fd=%d offset=%lu bytes=%lu -> OST %d\n", $fd, $offset, $bytes, $ost);
    @ost_bytes[$ost] = sum($bytes);
    @ost_ops[$ost] = count();
}
```

#### Run Command:
```bash
./bin/ubpftrace -c "examples/apps/lustre_io_app" ./examples/lustre_profiler.bt
```

#### Sample Output:
```text
Attached 1 probe
[LustreApp] Target file opened: /pscratch/sd/s/sgkim/scratch_lustre_test.dat (fd=10)
pwrite64 fd=10 offset=0 bytes=1048576 -> OST 73
pwrite64 fd=10 offset=1048576 bytes=1048576 -> OST 74
pwrite64 fd=10 offset=2097152 bytes=1048576 -> OST 75
pwrite64 fd=10 offset=3145728 bytes=1048576 -> OST 76
pwrite64 fd=10 offset=4194304 bytes=1048576 -> OST 73
pwrite64 fd=10 offset=5242880 bytes=1048576 -> OST 74
pwrite64 fd=10 offset=6291456 bytes=1048576 -> OST 75
pwrite64 fd=10 offset=7340032 bytes=1048576 -> OST 76
[LustreApp] Done.

@ost_bytes[73]: 2097152
@ost_bytes[74]: 2097152
@ost_bytes[75]: 2097152
@ost_bytes[76]: 2097152
@ost_ops[73]: 2
@ost_ops[74]: 2
@ost_ops[75]: 2
@ost_ops[76]: 2
```

---

## 🚀 Multi-Node HPC Dynamic Tracing & Offline Toolchain

`ubpftrace` features a high-throughput, Lustre-aligned multi-node engine designed to trace distributed MPI applications across thousands of nodes with nanosecond latency:

```
  Node 0 (Compute Node)                   Node 1 (Compute Node)
  ├── Rank 0 (Producer) ──┐               ├── Rank 2 (Producer) ──┐
  ├── Rank 1 (Producer) ──┼─> Node SHM    ├── Rank 3 (Producer) ──┼─> Node SHM
  └── I/O Worker ─────────┘   (Epoch-     └── I/O Worker ─────────┘   (Epoch-
      │                        Protected)     │                        Protected)
      ▼ (2MB Aligned LZ4)                     ▼ (2MB Aligned LZ4)
  `ubpftrace_<job>_node_0.ubpf`           `ubpftrace_<job>_node_1.ubpf`
                      │                       │
                      └───────────┬───────────┘
                                  ▼
                     `ubpftrace-cat` Decoder / Merger
                     ├── --info   : Chunk metadata & compression ratio
                     ├── --dump   : Formatted text event dump
                     ├── --merge  : Multi-stream min-heap chronological merge
                     └── --chrome : Chrome Tracing timeline (chrome://tracing)
```

### 1. Launching Tracing Across Multi-Node Slurm Jobs

Set `UBPFTRACE_OUTPUT_DIR` to a shared parallel filesystem path (e.g., Lustre `$SCRATCH`):

```bash
# Launch 4 ranks across 2 nodes tracing simulate_computation()
salloc -N 2 -C cpu -q interactive -t 00:10:00 -- \
  srun -N 2 -n 4 env UBPFTRACE_OUTPUT_DIR=$SCRATCH/traces \
    ./bin/ubpftrace -c "./examples/apps/hpc_app" -e '
      uprobe:./examples/apps/hpc_app:simulate_computation {
          printf("Rank %d iter %d start\n", rank, arg1);
      }
    '
```

### 2. Generated Tracing Artifacts

1. **Per-Node Trace Containers (`.ubpf`)**:
   `ubpftrace_<jobid>_node_<nid>.ubpf` contains 2MB stripe-aligned, LZ4-compressed binary chunks with CRC32 integrity verification. Exactly one file is written per physical compute node, protecting Lustre Metadata Servers (MDS) from file explosion.
2. **Consolidated Summary Profile (`_summary.json`)**:
   `ubpftrace_<jobid>_summary.json` is generated at `MPI_Finalize` via a Score-P style binomial tree reduction across an isolated private communicator (`MPI_Comm_dup`), aggregating per-rank recorded and dropped event counts.

### 3. Inspecting & Decoding Traces with `ubpftrace-cat`

The repository includes `ubpftrace-cat` in `bin/` for high-throughput out-of-band trace analysis:

## ⚡ Real-Time Observability & Monitoring Options

While post-run time-bucketed analysis (Scenario B) guarantees maximum application throughput and strict zero-jitter invariants, `ubpftrace` provides two powerful real-time observability modes:

### 1. Periodic Metric Snapshotting (Scenario A)
Inspect running aggregations without blocking compute threads or issuing global MPI collectives:
- `-L, --live <SEC>`: Interval (in seconds) for periodic Scenario A metric snapshotting.
- `--live-ms <MS>`: Sub-second interval (in milliseconds) down to 50ms for rapid metric inspection.
- `--live-dir <DIR>`: Custom output directory for out-of-band atomic JSON snapshots (`node_<nid>.json`). Snapshots are generated via hidden temporary files and atomic POSIX `rename()` to guarantee parse-tear-free telemetry.

```bash
# Snapshot metrics every 500ms to a shared directory:
./bin/ubpftrace --live-ms 500 --live-dir /tmp/ubpf_snapshots -c "examples/apps/hpc_app" presets/mpi_straggler_detector.bt
```

### 2. Low-Latency Micro-Buffered Live Event Streaming
Stream formatted traces (`printf(...)`) in real time to stdout with sub-frame latency and strict intra-node chronological monotonicity:
- `--stream`: Enable micro-buffered live terminal event streaming.
- `--stream-flush-ms <MS>`: Soft-timer flush timeout in milliseconds (default: `20ms`).
- `--stream-buffer-kb <KB>`: Buffer size threshold in kilobytes (default: `8KB`).

```bash
# Stream trace events with 20ms maximum latency:
./bin/ubpftrace --stream --stream-flush-ms 20 --stream-buffer-kb 8 -c "examples/apps/puts_app" -e 'uprobe:libc:puts { printf("puts: %s\n", str(arg0)); }'
```

---

## 📊 Live Cluster Dashboard (`ubpftrace-top`)

`ubpftrace-top` is a real-time cluster monitoring dashboard that continuously polls and aggregates Scenario A metric snapshots across all compute nodes:

```
================================================================================
 ubpftrace-top :: Real-Time Cluster Aggregation Dashboard (Cycle #1)
================================================================================
 Snapshot Dir : /tmp/ubpf_snapshots
 Active Nodes : 64 | Stragglers: 2 | Interval: 1s
--------------------------------------------------------------------------------

[CLUSTER-WIDE METRIC AGGREGATIONS]
Map Name                        Global Max    Global Min      Global Sum Entries
--------------------------------------------------------------------------------
@barrier_lat_us                     64210            12          1984200     192
@allreduce_lat_us                     450             8            23100     192

[NODE TOPOLOGY & SYNC STATUS]
Node ID   Hostname            Epoch     Latency Lag (ms)    Status         
---------------------------------------------------------------------------
1001      nid004220           142       2.10                [HEALTHY]
1002      nid004221           142       1.85                [HEALTHY]
1003      nid004222           138       420.50              [STRAGGLER]
```

### Usage Modes:
- **Interactive ANSI TUI**:
  ```bash
  ./bin/ubpftrace-top --dir /tmp/ubpf_snapshots --interval 1
  ```
- **Automated JSON Streaming Pipeline (for Grafana / PromQL ingest)**:
  ```bash
  ./bin/ubpftrace-top --dir /tmp/ubpf_snapshots --json --interval 2
  ```
- **Single-Shot Verification**:
  ```bash
  ./bin/ubpftrace-top --dir /tmp/ubpf_snapshots --once
  ```

---

## 🛠️ Offline Trace Processing Toolchain (`ubpftrace-cat`)

`ubpftrace-cat` is a high-performance offline decoder for `.ubpf` 2MB stripe-aligned container files:

### 1. Inspect Container Metadata & Compression Savings
```bash
./bin/ubpftrace-cat --info traces/ubpftrace_1111_node_0.ubpf
```
Outputs total chunk counts, uncompressed vs compressed sizes, CRC32 block validations, and LZ4 compression savings.

### 2. Stream Formatted Text Event Logs
```bash
./bin/ubpftrace-cat --dump traces/ubpftrace_1111_node_0.ubpf
```

### 3. Multi-Node Chronological Merge (K-Way Min-Heap)
```bash
# Globally order events from hundreds of node containers into a unified timestamp stream:
./bin/ubpftrace-cat --merge traces/ubpftrace_1111_node_*.ubpf
```

### 4. Export to Google Chrome / Perfetto Timeline
```bash
# Export container events to Google Chrome Trace Event format:
./bin/ubpftrace-cat --chrome timeline.json traces/ubpftrace_1111_node_0.ubpf

# Load timeline.json in https://ui.perfetto.dev or chrome://tracing
```

---

## 🎯 Flagship Production Presets Suite (`presets/`)

`ubpftrace` includes 6 ready-to-use production diagnostic presets covering major HPC and Distributed AI performance bottlenecks:

| Preset Script | Workload Domain | Key Performance Metrics Diagnosed |
| :--- | :--- | :--- |
| **`ai_checkpoint_lustre.bt`** | Large-scale AI / LLM Training | Parallel I/O checkpoint bandwidth, latency histograms, per-OST stripe distribution (`lustre_ost`), and storage hotspot detection. |
| **`mpi_straggler_detector.bt`** | Multi-Node MPI Workloads | Per-rank barrier & collective wait time histograms, identifying compute load imbalance and delayed ranks. |
| **`mpi_p2p_traffic.bt`** | HPC Scientific Simulations | Per-rank send/recv volumes, halo-exchange message size log2 distributions, and point-to-point traffic patterns. |
| **`openmp_hybrid_contention.bt`** | Hybrid MPI + OpenMP Jobs | OpenMP thread barrier wait latency, critical section lock contention (`GOMP_critical_start`), and parallel region overheads. |
| **`cuda_sync_bubbles.bt`** | GPU Accelerated Computing | Host-side synchronization bubbles (`cudaStreamSynchronize`, `cudaDeviceSynchronize`, `cudaMemcpy`) causing GPU pipeline stalls. |
| **`nccl_collective_skew.bt`** | Distributed AI (PyTorch FSDP/Megatron) | Multi-GPU collective communication tail latency (`ncclAllReduce`, `ncclReduceScatter`, `ncclAllGather`), tensor volumes, and rank skew. |

### Running Presets:
Presets are automatically discovered and can be referenced by relative path:
```bash
# Trace Lustre parallel checkpoint I/O:
./bin/ubpftrace -c "python train_fsdp.py" presets/ai_checkpoint_lustre.bt

# Trace NCCL collective skew across multi-GPU ranks:
srun -N 2 -n 8 ./bin/ubpftrace -c "torchrun train.py" presets/nccl_collective_skew.bt
```

---

## 🏗️ Architecture & Mechanism

```
                  ┌──────────────────────────────────────────────────┐
                  │                 ubpftrace CLI                    │
                  │   - Clang/LLVM AST Parser & Type Checker         │
                  │   - Bytecode Generator (LLVM IR -> eBPF)         │
                  │   - POSIX Shared Memory Coordinator              │
                  └─────────────────────────┬────────────────────────┘
                                            │ Spawns child / injects
                                            ▼
                  ┌──────────────────────────────────────────────────┐
                  │            Target Application (User Mode)        │
                  │                                                  │
                  │   ┌───────────────────────────────────────────┐  │
                  │   │        libbpftime-agent.so                │  │
                  │   │  1. Inlines 5-byte JMP (Frida-Gum)        │  │
                  │   │  2. Maps CPU registers into pt_regs frame │  │
                  │   │  3. Executes eBPF in LLVM JIT VM          │  │
                  │   │  4. Writes output to POSIX SHM RingBuf    │  │
                  │   └───────────────────────────────────────────┘  │
                  │                                                  │
                  │   int calculate(int a, int b) {                  │
                  │   └──> [Inline Trampoline] ──> [LLVM JIT eBPF]   │
                  │   }                                              │
                  └──────────────────────────────────────────────────┘
```

1. **Self-Bootstrapping Mock Syscall Server**:
   When `ubpftrace` starts, it preloads `libbpftime-syscall-server.so`. This intercepts `SYS_bpf` and `SYS_perf_event_open` syscalls, creating eBPF programs and maps inside POSIX shared memory (`/dev/shm/bpftime_shm`) instead of calling the Linux kernel.
2. **Inline Trampoline Hooking (Frida-Gum)**:
   When tracing targets with `-c <command>`, `ubpftrace` preloads `libbpftime-agent.so`. The agent resolves the target function symbol in the ELF `.symtab` / `.dynsym` and overwrites the function prologue with an atomic 5-byte `JMP` instruction pointing to a trampoline.
3. **Register Mapping & JIT Execution**:
   The trampoline captures general-purpose CPU registers into a `pt_regs` structure, sets up argument registers, and executes the compiled eBPF program directly inside the application process using LLVM JIT (`llvmbpf`).
4. **Zero Context Switching**:
   Execution never enters kernel space (Ring 0). No breakpoint interrupts (`INT3`), no scheduler context switches, and no signal handlers are involved.

---

## 📂 Repository Structure

```
ubpftrace/
├── bin/
│   ├── ubpftrace                      # Main CLI compiler & tracer executable
│   ├── ubpftrace-cat                  # Standalone trace container decoder, merger & Chrome exporter
│   ├── ubpftrace-top                  # Real-time cluster dashboard & JSON telemetry streamer
│   ├── libbpftime-agent.so            # Userspace runtime agent (Frida-Gum + JIT)
│   ├── libbpftime-syscall-server.so   # Mock syscall server for libbpf
│   └── test_hpc_shm_io                # Concurrency & zero-jitter SHM stress benchmark
├── bpftime/                           # Complete embedded bpftime runtime subsystem
│   └── runtime/
│       ├── include/hpc/               # HPC data plane headers (Lustre, SHM, Reducer, Live, Stream)
│       └── src/hpc/                   # HPC data plane implementations
├── src/                               # bpftrace script compiler frontend (AST, LLVM IR, parser)
├── presets/                           # Production HPC & Distributed AI diagnostic presets
│   ├── ai_checkpoint_lustre.bt        # Lustre OST parallel I/O profiler
│   ├── mpi_straggler_detector.bt      # Multi-node MPI barrier & collective straggler analyzer
│   ├── mpi_p2p_traffic.bt             # MPI point-to-point communication matrix
│   ├── openmp_hybrid_contention.bt    # OpenMP barrier & critical section profiler
│   ├── cuda_sync_bubbles.bt           # CUDA host-side synchronization stall tracker
│   └── nccl_collective_skew.bt        # NCCL multi-GPU collective tail latency analyzer
├── examples/                          # Ready-to-run interactive examples & sample applications
│   ├── calc.bt                        # Traces user-defined calculate(int, int) function
│   ├── puts.bt                        # Traces libc:puts string function
│   ├── malloc.bt                      # Traces libc:malloc memory allocations & hist()
│   ├── math.bt                        # Traces libm:sqrt math library function
│   ├── struct_trace.bt                # Traces custom C structs, stats() & multi-key maps
│   └── interval.bt                    # Demonstrates periodic userspace interval timers
├── run_tests.sh                       # End-to-end integration test runner
└── CMakeLists.txt                     # Unified build configuration
```

---

## 📄 License

`ubpftrace` is distributed under the [Apache 2.0 License](LICENSE) and [GPL-2.0](bpftime/LICENSE) for respective components.
