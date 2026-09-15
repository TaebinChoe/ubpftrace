# Getting Started with ubpftrace

This quickstart guide gets you up and running with `ubpftrace` in under 5 minutes. You will learn how to build the project, write your first userspace eBPF tracing script, monitor running workloads in real time, and analyze trace containers.

---

## 1. Prerequisites & Single-Step Build

`ubpftrace` runs completely in **unprivileged userspace** without requiring root access, `sudo`, or Linux kernel `CAP_BPF` permissions.

### System Requirements
- Linux OS (x86_64, Linux kernel 4.x or later)
- Modern C++ compiler (`gcc >= 9` or `clang >= 11`)
- CMake `>= 3.20`
- OpenMPI or MPICH (Optional, for multi-node MPI features)

### Build Command
Run the unified build script from the repository root:

```bash
git clone https://github.com/TaebinChoe/ubpftrace.git
cd ubpftrace

# Execute unified build script
./scripts/build_hpc.sh
```

Upon completion, all binaries are built in `bin/`:
- `bin/ubpftrace`: The main compiler frontend and tracer
- `bin/ubpftrace-cat`: Standalone `.ubpf` trace container decoder & Perfetto exporter
- `bin/ubpftrace-top`: Real-time cluster TUI dashboard

---

## 2. Your First Tracing Script

Let's trace a simple C program. Create a sample file `simple.c`:

```c
// simple.c
#include <stdio.h>
#include <unistd.h>

void foo(int val) {
    usleep(100000); // 100ms sleep
}

int main() {
    printf("Starting target application...\n");
    for (int i = 0; i < 20; i++) {
        foo(i);
    }
    printf("Target application finished.\n");
    return 0;
}
```

Compile the application with debugging symbols:
```bash
gcc -O0 -g -o simple simple.c
```

### Trace Function Entries & Arguments
In `ubpftrace`, you can trace function calls dynamically using the `-e` flag:

```bash
./bin/ubpftrace -c "./simple" -e '
uprobe:./simple:foo {
    printf("foo() called with val = %d\n", arg0);
}
'
```

**Output**:
```text
Attached 1 probe
Starting target application...
foo() called with val = 0
foo() called with val = 1
foo() called with val = 2
...
foo() called with val = 19
Target application finished.
```

> [!TIP]
> **Argument Mapping (x86_64 ABI)**:
> In `ubpftrace`, arguments are 0-indexed:
> - `arg0` = 1st parameter (`%rdi`)
> - `arg1` = 2nd parameter (`%rsi`)
> - `arg2` = 3rd parameter (`%rdx`)

---

## 3. High-Performance In-Memory Map Aggregations

Instead of printing every single function call (which can flood your terminal on high-frequency loops), `ubpftrace` provides **lock-free BPF maps** that aggregate statistics directly in RAM with zero disk I/O:

```bash
./bin/ubpftrace -c "./simple" -e '
uprobe:./simple:foo {
    @call_count = count();
    @sum_val = sum(arg0);
    @avg_val = avg(arg0);
    @arg_distribution = hist(arg0);
}
'
```

**Output on Process Exit**:
```text
Attached 1 probe
Starting target application...
Target application finished.

@avg_val: 9
@call_count: 20
@sum_val: 190

@arg_distribution: 
[0]                    1 |@@@@                                    |
[1]                    1 |@@@@                                    |
[2, 4)                 2 |@@@@@@@@                                |
[4, 8)                 4 |@@@@@@@@@@@@@@@@                        |
[8, 16)                8 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@        |
[16, 32)               4 |@@@@@@@@@@@@@@@@                        |
```

---

## 4. Real-Time Telemetry & Dashboards

`ubpftrace` provides two ways to inspect metrics while the program is running:

### Option A: Micro-Buffered Live Terminal Stream (`--stream`)
Stream low-latency events to stdout with micro-buffering (20ms / 8KB) without stalling compute threads:

```bash
./bin/ubpftrace --stream -c "./simple" -e '
uprobe:./simple:foo {
    printf("[TS %lu ns] foo(val=%d) executed\n", nsecs, arg0);
}
'
```

### Option B: Out-of-Band Periodic Snapshots & Live TUI Dashboard (`ubpftrace-top`)
For long-running compute jobs and multi-node clusters, decouple tracing from monitoring:

1. **Launch Tracing with 1-Second Live Snapshots**:
   ```bash
   mkdir -p ./live_snapshots
   ./bin/ubpftrace -L 1 --live-dir ./live_snapshots -c "./simple" -e '
   uprobe:./simple:foo {
       @calls = count();
       @sum = sum(arg0);
   }
   '
   ```

2. **In a Second Terminal, Launch `ubpftrace-top`**:
   ```bash
   ./bin/ubpftrace-top --dir ./live_snapshots
   ```
   *An interactive ANSI dashboard opens, displaying live call rates, active nodes, and metric tables updated in real time.*

---

## 5. Decoding Persistent Trace Containers (`ubpftrace-cat`)

When capturing binary container files (`.ubpf`), use `ubpftrace-cat` to inspect or visualize traces:

```bash
# 1. Inspect container metadata and LZ4 compression stats
./bin/ubpftrace-cat --info trace_node_0.ubpf

# 2. Dump all raw event records chronologically
./bin/ubpftrace-cat --dump trace_node_0.ubpf

# 3. Export to Google Chrome Tracing / Perfetto format
./bin/ubpftrace-cat --merge trace_node_*.ubpf --chrome cluster_timeline.json
```

Open **[ui.perfetto.dev](https://ui.perfetto.dev)** in your browser and open `cluster_timeline.json` to view interactive Gantt charts with rank swimlanes and latency timelines.

---

## Next Steps
- Learn advanced scripting, filters, and HPC builtins in the [Scripting & User Guide](user_guide.md).
- Deploy on Slurm clusters with MPI and Lustre in the [HPC & MPI Guide](hpc_and_mpi_guide.md).
- Explore ready-to-run scripts in the [Production Presets Catalog](presets_catalog.md).
