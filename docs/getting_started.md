# Getting Started with ubpftrace

This tutorial provides a complete walkthrough of `ubpftrace` from your first probe to cluster-scale telemetry. All examples use the minimal test target application located in [`examples/getting_started/`](../examples/getting_started/).

> [!NOTE]
> **Getting Started vs. Production Presets**:
> - **[`examples/getting_started/`](../examples/getting_started/)**: Explains core mechanics and basic usage using the simplest C workload and `.bt` scripts.
> - **[`presets/`](../presets/)**: Production-ready scripts designed for realistic high-performance environments (e.g., MPI point-to-point traffic, collective skew, CUDA stream synchronization, and Lustre I/O profiling).

---

## 1. Quick Build & Setup

`ubpftrace` operates entirely in **unprivileged userspace**. It requires no `sudo`, no kernel modules, and no Linux `CAP_BPF` permissions.

### Build Binaries
```bash
sgkim@login05:/pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace$ ./scripts/build_hpc.sh
```

Upon completion, all executables are placed in `bin/`:
- **`bin/ubpftrace`**: Main compiler frontend and dynamic tracer.
- **`bin/ubpftrace-top`**: Real-time TUI cluster monitoring dashboard.
- **`bin/ubpftrace-cat`**: Standalone container reader, decoder, and Perfetto/Chrome-trace exporter.

---

## 2. The Target Workload Application

All getting-started scenarios trace [`examples/getting_started/target_app.c`](../examples/getting_started/target_app.c), a lightweight C application that executes a parameterized function `compute_task(int task_id, int data_size)`:

```c
// examples/getting_started/target_app.c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void compute_task(int task_id, int data_size) {
    usleep(100000); // 100ms simulated work
}

int main(int argc, char **argv) {
    int total_tasks = (argc > 1) ? atoi(argv[1]) : 20;
    printf("[TargetApp] Starting execution of %d tasks...\n", total_tasks);

    for (int i = 0; i < total_tasks; i++) {
        int data_size = (i * 17 + 10) % 100;
        compute_task(i, data_size);
    }
    printf("[TargetApp] Finished all tasks successfully.\n");
    return 0;
}
```

Compile the application:
```bash
sgkim@login05:/pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace$ make -C examples/getting_started
```

---

## 3. Scenario 1: Function Tracing & Argument Extraction

Dynamic probes (`uprobe`) attach to functions by binary path and symbol name. Arguments are accessed using `arg0`, `arg1`, etc. (mapped to x86_64 ABI registers `%rdi`, `%rsi`, etc.).

### Script: [`01_function_tracing.bt`](../examples/getting_started/01_function_tracing.bt)
```bt
uprobe:./examples/getting_started/target_app:compute_task {
    printf("[TS %lu ns] compute_task() invoked -> task_id=%d, data_size=%d bytes\n",
           nsecs, arg0, arg1);
}
```

### Execution & Real Terminal Output
```bash
sgkim@login05:/pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace$ ./bin/ubpftrace -c "./examples/getting_started/target_app 5" examples/getting_started/01_function_tracing.bt
```

```text
Attached 1 probe
[TargetApp] Starting execution of 5 tasks (~0.5 seconds)...
[TS 113086036814292 ns] compute_task() invoked -> task_id=0, data_size=10 bytes
[TS 113086136885820 ns] compute_task() invoked -> task_id=1, data_size=27 bytes
[TS 113086236946022 ns] compute_task() invoked -> task_id=2, data_size=44 bytes
[TS 113086337005433 ns] compute_task() invoked -> task_id=3, data_size=61 bytes
[TS 113086437060412 ns] compute_task() invoked -> task_id=4, data_size=78 bytes
[TargetApp] Progress: 5 / 5 tasks (current task_id=4, data_size=78)
[TargetApp] Finished all tasks successfully.
```

---

## 4. Scenario 2: Zero-Overhead In-Memory Map Aggregations

Printing every event can produce significant I/O overhead on high-frequency loops. `ubpftrace` maps maintain lock-free counters, sums, and distributions directly in memory (`/dev/shm`) with **zero disk writes** during execution.

### Script: [`02_map_aggregation.bt`](../examples/getting_started/02_map_aggregation.bt)
```bt
uprobe:./examples/getting_started/target_app:compute_task {
    $size = arg1;

    @total_tasks = count();
    @data_stats = stats($size);
    @data_size_hist = hist($size);
}
```

### Execution & Real Terminal Output
```bash
sgkim@login05:/pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace$ ./bin/ubpftrace -c "./examples/getting_started/target_app 20" examples/getting_started/02_map_aggregation.bt
```

```text
Attached 1 probe
[TargetApp] Starting execution of 20 tasks (~2.0 seconds)...
[TargetApp] Progress: 10 / 20 tasks (current task_id=9, data_size=63)
[TargetApp] Progress: 20 / 20 tasks (current task_id=19, data_size=33)
[TargetApp] Finished all tasks successfully.


@data_size_hist:
[8, 16)                3 |@@@@@@@@@@@@@@@@@@@@@@                              |
[16, 32)               4 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@                       |
[32, 64)               6 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@        |
[64, 128)              7 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|

@data_stats: { .count = 20, .average = 51, .total = 1030 }
@total_tasks: 20
```

---

## 5. Scenario 3: Periodic Time-Window Aggregations & Throttled Streams

To monitor trends over time without unbounded map memory growth, you can index maps by time epoch windows (`$window_sec = elapsed / 1000000000`) and emit a throttled live stream notification once per window.

### Script: [`03_windowed_metrics.bt`](../examples/getting_started/03_windowed_metrics.bt)
```bt
uprobe:./examples/getting_started/target_app:compute_task {
    $window_sec = elapsed / 1000000000;
    $size = arg1;

    // 1. Time-windowed map metrics (bounded memory)
    @window_task_count[$window_sec] = count();
    @window_data_stats[$window_sec] = stats($size);

    // 2. Rate-limited event stream (triggers only at the start of each new second)
    if ($window_sec != @last_printed_window) {
        @last_printed_window = $window_sec;
        printf("[Stream @ %lu s] Window #%lu started | Sample task_id=%d, data_size=%d\n",
               $window_sec, $window_sec, arg0, $size);
    }
}
```

### Execution & Real Terminal Output
```bash
sgkim@login05:/pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace$ ./bin/ubpftrace --no-warnings -c "./examples/getting_started/target_app 25" examples/getting_started/03_windowed_metrics.bt
```

```text
Attached 1 probe
[TargetApp] Starting execution of 25 tasks (~2.5 seconds)...
[Stream @ 0 s] Window #0 started | Sample task_id=0, data_size=10
[TargetApp] Progress: 10 / 25 tasks (current task_id=9, data_size=63)
[Stream @ 1 s] Window #1 started | Sample task_id=10, data_size=80
[TargetApp] Progress: 20 / 25 tasks (current task_id=19, data_size=33)
[Stream @ 2 s] Window #2 started | Sample task_id=20, data_size=50
[TargetApp] Progress: 25 / 25 tasks (current task_id=24, data_size=18)
[TargetApp] Finished all tasks successfully.


@last_printed_window: 2
@window_data_stats[2]: { .count = 5, .average = 52, .total = 260 }
@window_data_stats[1]: { .count = 10, .average = 48, .total = 485 }
@window_data_stats[0]: { .count = 10, .average = 53, .total = 535 }
@window_task_count[2]: 5
@window_task_count[1]: 10
@window_task_count[0]: 10
```

---

## 6. Scenario 4: Live Cluster Dashboard with `ubpftrace-top`

`ubpftrace` can export lock-free map snapshots periodically out-of-band to a shared directory. `ubpftrace-top` reads these snapshots and renders a cluster-wide aggregated TUI dashboard without interrupting target processes.

### Script: [`04_live_top_dashboard.bt`](../examples/getting_started/04_live_top_dashboard.bt)
```bt
uprobe:./examples/getting_started/target_app:compute_task {
    $size = arg1;

    @total_tasks = count();
    @total_bytes = sum($size);
    @data_size_distribution = hist($size);
}
```

### Step 1: Run Tracing with Live Snapshots Enabled
```bash
# Terminal 1
sgkim@login05:/pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace$ UBPFTRACE_LIVE_DIR=./live_demo ./bin/ubpftrace --live-ms 300 -c "./examples/getting_started/target_app 30" examples/getting_started/04_live_top_dashboard.bt
```

### Step 2: Launch `ubpftrace-top`
```bash
# Terminal 2
sgkim@login05:/pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace$ ./bin/ubpftrace-top -d ./live_demo
```

**Real Terminal Output from `ubpftrace-top`:**
```text
================================================================================
 ubpftrace-top :: Real-Time Cluster Aggregation Dashboard (Cycle #1)
================================================================================
 Snapshot Dir : /pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/examples/getting_started/.live_demo
 Active Nodes : 1 | Stragglers: 0 | Interval: 1s
--------------------------------------------------------------------------------

[CLUSTER-WIDE METRIC AGGREGATIONS]
Map Name                        Global Max    Global Min      Global Sum Entries
--------------------------------------------------------------------------------
AT_data_size_di                          2             1               7       4
AT_total_bytes                         327           327             327       1
AT_total_tasks                           7             7               7       1

[NODE TOPOLOGY & SYNC STATUS]
Node ID   Hostname            Epoch     Latency Lag (ms)    Status
---------------------------------------------------------------------------
3650575891login18             1         0.00                [HEALTHY]

[Press Ctrl+C to stop monitoring]
```

---

## 7. Scenario 5: High-Speed Trace Containers (`.ubpf`) & `ubpftrace-cat`

When detailed event traces are required, `ubpftrace` offloads events to an unpinned ring buffer. A dedicated background I/O worker compresses chunks with LZ4 and writes them into high-performance `.ubpf` containers.

### Script: [`05_trace_container_cat.bt`](../examples/getting_started/05_trace_container_cat.bt)
```bt
uprobe:./examples/getting_started/target_app:compute_task {
    printf("[TS %lu ns] Task %d processed data_size=%d bytes\n",
           nsecs, arg0, arg1);
}
```

### Step 1: Record Trace to Container
```bash
sgkim@login05:/pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace$ UBPFTRACE_OUTPUT_DIR=./examples/getting_started ./bin/ubpftrace -c "./examples/getting_started/target_app 10" examples/getting_started/05_trace_container_cat.bt
```

### Step 2: Inspect Container Metadata (`ubpftrace-cat --info`)
```bash
sgkim@login05:/pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace$ ./bin/ubpftrace-cat --info examples/getting_started/*.ubpf
```

```text
============================================================
  UBPFTRACE CONTAINER METADATA: /pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/examples/getting_started/ubpftrace_1014833_node_3650575891.ubpf
============================================================
  Job ID            : 1014833
  Node ID           : 3650575891
  Hostname          : login18
  File Size         : 628 bytes (0.00 MB)
  Codec             : LZ4
  Base Monotonic Ts : 113086565071405 ns
  Base Wallclock Ts : 1789708470377293701 ns
  Total Chunks      : 1
  Total Records     : 10
  Total Dropped     : 0
  Uncompressed Size : 840 bytes
  Compressed Size   : 356 bytes (Savings: 57.6%)

Chunk Details:
  Chunk#  Offset      UncompSize    CompSize      Records   Dropped   CRC32     
  ----------------------------------------------------------------------
  0       128         840           356           10        0         OK
============================================================
```

### Step 3: Dump Chronological Events (`ubpftrace-cat --dump`)
```bash
sgkim@login05:/pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace$ ./bin/ubpftrace-cat --dump examples/getting_started/*.ubpf
```

```text
[113086.565397s] [Node 3650575891] [Rank 0] [Event 1] [TS 113086538302792 ns] Task 0 processed data_size=10 bytes
[113086.639523s] [Node 3650575891] [Rank 0] [Event 1] [TS 113086638374488 ns] Task 1 processed data_size=27 bytes
[113086.739971s] [Node 3650575891] [Rank 0] [Event 1] [TS 113086738434281 ns] Task 2 processed data_size=44 bytes
[113086.840313s] [Node 3650575891] [Rank 0] [Event 1] [TS 113086838493513 ns] Task 3 processed data_size=61 bytes
[113086.939643s] [Node 3650575891] [Rank 0] [Event 1] [TS 113086938548448 ns] Task 4 processed data_size=78 bytes
[113087.040020s] [Node 3650575891] [Rank 0] [Event 1] [TS 113087038608231 ns] Task 5 processed data_size=95 bytes
[113087.140420s] [Node 3650575891] [Rank 0] [Event 1] [TS 113087138666732 ns] Task 6 processed data_size=12 bytes
[113087.239836s] [Node 3650575891] [Rank 0] [Event 1] [TS 113087238724933 ns] Task 7 processed data_size=29 bytes
[113087.340422s] [Node 3650575891] [Rank 0] [Event 1] [TS 113087338798913 ns] Task 8 processed data_size=46 bytes
[113087.440803s] [Node 3650575891] [Rank 0] [Event 1] [TS 113087438877120 ns] Task 9 processed data_size=63 bytes
```

### Step 4: Export to Perfetto / Chrome Tracing Format
```bash
sgkim@login05:/pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace$ ./bin/ubpftrace-cat --chrome timeline.json examples/getting_started/*.ubpf
```

```text
Exported Chrome Trace Event format to: timeline.json
```
Open **[ui.perfetto.dev](https://ui.perfetto.dev)** and drag-and-drop `timeline.json` to inspect interactive timelines, rank swimlanes, and execution latencies.

---

## 8. Summary & Next Steps

| Workload Requirement | Recommended Approach | Tooling |
| :--- | :--- | :--- |
| **Simple Call & Arg Inspection** | Function tracing with `printf()` | `ubpftrace -c ...` |
| **High-Frequency Statistics** | In-memory lock-free BPF maps (`count()`, `hist()`, `stats()`) | `ubpftrace` (summarized at exit) |
| **Periodic / Bounded Metrics** | Time-window epoch keys (`elapsed / 1s`) | `ubpftrace` |
| **Cluster-Wide Online Telemetry** | Periodic JSON snapshots (`--live-ms 300`) | `ubpftrace-top` |
| **High-Volume Timeline Analysis** | LZ4 container offloading (`.ubpf`) | `ubpftrace-cat` + Perfetto |

- Ready to profile multi-node MPI applications and GPU workloads? Check out the **[Production Presets Catalog](../presets/README.md)**.
- For MPI/Slurm environment setup and Lustre parallel filesystem tuning, see the **[HPC & MPI Guide](hpc_and_mpi_guide.md)**.
