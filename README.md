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

On Ubuntu 22.04 / 24.04 or Debian-based systems, install the required build dependencies:

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

---

## 📦 Building from Source (Copy & Paste)

Clone and build the entire `ubpftrace` toolchain in one simple step:

```bash
# 1. Clone and navigate to the repository
git clone https://github.com/TaebinChoe/ubpftrace.git
cd ubpftrace

# 2. Build everything (ubpftrace frontend + bpftime runtime agent) at once
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=OFF
cmake --build build -j$(nproc)

echo ">>> ubpftrace and libraries successfully built in bin/ !"
```

All compiled binaries (`bin/ubpftrace`, `bin/libbpftime-agent.so`, `bin/libbpftime-syscall-server.so`) are generated automatically inside the `bin/` directory.


Verify the installation:

```bash
./bin/ubpftrace --version
```
*(Outputs `bpftrace v0.27.0` cleanly without requiring `sudo`)*

---

## 🚀 1-Minute Quick Start

### 1. Tracing a Standard Command One-Liner
Trace dynamic memory allocations in `/bin/ls` without `sudo`:

```bash
./bin/ubpftrace -c "/bin/ls" -e 'uprobe:libc:malloc { printf("malloc(%d bytes)\n", arg0); @count = count(); }'
```

---

## 🧪 Interactive Examples & Test Suite

The repository includes ready-to-run test cases under the [`examples/`](examples) directory.

### Run All Integration Tests
Execute the automated test suite verifying all 6 subsystems (compiles sample apps automatically):

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

#### Run Command:
```bash
./bin/ubpftrace -c "examples/apps/hpc_app" ./examples/mpi_bottleneck.bt
```

#### Sample Output:
```text
@allreduce_latency_us: 
[0]                    2 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
[1]                    1 |@@@@@@@@@@@@@@@@@@@@@@@@@@                               |
@allreduce_stats_us: { .count = 3, .average = 0, .total = 1 }

@barrier_latency_us: 
[0]                    2 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
[1]                    1 |@@@@@@@@@@@@@@@@@@@@@@@@@@                               |
@barrier_stats_us: { .count = 3, .average = 0, .total = 1 }

@mpi_call_counts[MPI_Allreduce]: 3
@mpi_call_counts[MPI_Barrier]: 3
@mpi_call_counts[MPI_Recv]: 3
@mpi_call_counts[MPI_Send]: 3
@send_msg_elements_hist: 
[256, 512)             3 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
@send_volume_by_dest[0]: 768
@total_barrier_time_us: 1
@total_send_elements: 768
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
│   ├── libbpftime-agent.so            # Userspace runtime agent (Frida-Gum + JIT)
│   └── libbpftime-syscall-server.so   # Mock syscall server for libbpf
├── bpftime/                           # Complete embedded bpftime runtime subsystem
├── src/                               # bpftrace script compiler frontend (AST, LLVM IR, parser)
├── examples/
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
