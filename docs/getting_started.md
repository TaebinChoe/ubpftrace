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
./scripts/build_hpc.sh
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
    printf("[TargetApp] Starting execution of %d tasks (~%.1f seconds)...\n",
           total_tasks, (double)total_tasks * 0.1);

    for (int i = 0; i < total_tasks; i++) {
        int data_size = (i * 17 + 10) % 100;
        compute_task(i, data_size);

        if ((i + 1) % 10 == 0 || (i + 1) == total_tasks) {
            printf("[TargetApp] Progress: %d / %d tasks (current task_id=%d, data_size=%d)\n",
                   i + 1, total_tasks, i, data_size);
        }
    }
    printf("[TargetApp] Finished all tasks successfully.\n");
    return 0;
}
```

### Compile the Application:
```bash
make -C examples/getting_started
```

**Output:**
```text
make: Entering directory '/pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/examples/getting_started'
/usr/bin/gcc -O2 -g -fno-inline -fno-omit-frame-pointer -o target_app target_app.c
make: Leaving directory '/pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/examples/getting_started'
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

### Run Command:
```bash
./bin/ubpftrace -c "./examples/getting_started/target_app 5" examples/getting_started/01_function_tracing.bt
```

**Real Output:**
```text
Attached 1 probe
[TargetApp] Starting execution of 5 tasks (~0.5 seconds)...
[TS 114175885900464 ns] compute_task() invoked -> task_id=0, data_size=10 bytes
[TS 114175985973386 ns] compute_task() invoked -> task_id=1, data_size=27 bytes
[TS 114176086055604 ns] compute_task() invoked -> task_id=2, data_size=44 bytes
[TS 114176186134977 ns] compute_task() invoked -> task_id=3, data_size=61 bytes
[TS 114176286226563 ns] compute_task() invoked -> task_id=4, data_size=78 bytes
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

### Run Command:
```bash
./bin/ubpftrace -c "./examples/getting_started/target_app 20" examples/getting_started/02_map_aggregation.bt
```

**Real Output:**
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

### Run Command:
```bash
./bin/ubpftrace --no-warnings -c "./examples/getting_started/target_app 25" examples/getting_started/03_windowed_metrics.bt
```

**Real Output:**
```text
Attached 1 probe
[TargetApp] Starting execution of 25 tasks (~2.5 seconds)...
[TargetApp] Progress: 10 / 25 tasks (current task_id=9, data_size=63)
[Stream @ 1 s] Window #1 started | Sample task_id=10, data_size=80
[TargetApp] Progress: 20 / 25 tasks (current task_id=19, data_size=33)
[Stream @ 2 s] Window #2 started | Sample task_id=20, data_size=50
[TargetApp] Progress: 25 / 25 tasks (current task_id=24, data_size=18)
[TargetApp] Finished all tasks successfully.


@last_printed_window: 2
@window_data_stats[2]: { .count = 5, .average = 44, .total = 220 }
@window_data_stats[0]: { .count = 10, .average = 46, .total = 465 }
@window_data_stats[1]: { .count = 10, .average = 56, .total = 565 }
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

### Step 1: Run Tracing in Terminal 1
```bash
UBPFTRACE_LIVE_DIR=./examples/getting_started/.live_demo \
  ./bin/ubpftrace --live-ms 300 -c "./examples/getting_started/target_app 30" \
  examples/getting_started/04_live_top_dashboard.bt
```

### Step 2: Open Terminal 2 and Launch `ubpftrace-top`
```bash
./bin/ubpftrace-top -d ./examples/getting_started/.live_demo
```

**Real `ubpftrace-top` Dashboard Output:**
```text
================================================================================
 ubpftrace-top :: Real-Time Cluster Aggregation Dashboard (Cycle #1)
================================================================================   
 Snapshot Dir : ./examples/getting_started/.live_demo
 Active Nodes : 1 | Stragglers: 0 | Interval: 1s
--------------------------------------------------------------------------------

[CLUSTER-WIDE METRIC AGGREGATIONS]
Map Name                        Global Max    Global Min      Global Sum Entries
--------------------------------------------------------------------------------
.data.event_los                          0             0               0       0
1093670_.rodata                          0             0               0       0
AT_data_size_di                          2             1               7       4
AT_total_bytes                         327           327             327       1
AT_total_tasks                           7             7               7       1
ringbuf                                  0             0               0       0

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

### Step 1: Record Traces to `.ubpf` Container
```bash
UBPFTRACE_OUTPUT_DIR=./examples/getting_started \
  ./bin/ubpftrace -c "./examples/getting_started/target_app 10" \
  examples/getting_started/05_trace_container_cat.bt
```

**Real Output:**
```text
Attached 1 probe
[TargetApp] Starting execution of 10 tasks (~1.0 seconds)...
[TS 114217681441997 ns] Task 0 processed data_size=10 bytes
[TS 114217781527321 ns] Task 1 processed data_size=27 bytes
[TS 114217881603528 ns] Task 2 processed data_size=44 bytes
[TS 114217981679604 ns] Task 3 processed data_size=61 bytes
[TS 114218081744611 ns] Task 4 processed data_size=78 bytes
[TS 114218181821639 ns] Task 5 processed data_size=95 bytes
[TS 114218281898567 ns] Task 6 processed data_size=12 bytes
[TS 114218381973682 ns] Task 7 processed data_size=29 bytes
[TS 114218482054056 ns] Task 8 processed data_size=46 bytes
[TS 114218582136084 ns] Task 9 processed data_size=63 bytes
[TargetApp] Progress: 10 / 10 tasks (current task_id=9, data_size=63)
[TargetApp] Finished all tasks successfully.
```

### Step 2: Inspect Container Metadata & LZ4 Compression (`ubpftrace-cat --info`)
```bash
./bin/ubpftrace-cat --info examples/getting_started/*.ubpf
```

**Real Output:**
```text
============================================================
  UBPFTRACE CONTAINER METADATA: examples/getting_started/ubpftrace_1057033_node_3650575891.ubpf
============================================================
  Job ID            : 1057033
  Node ID           : 3650575891
  Hostname          : login18
  File Size         : 631 bytes (0.00 MB)
  Codec             : LZ4
  Base Monotonic Ts : 114217709000751 ns
  Base Wallclock Ts : 1789709601521223056 ns
  Total Chunks      : 1
  Total Records     : 10
  Total Dropped     : 0
  Uncompressed Size : 840 bytes
  Compressed Size   : 359 bytes (Savings: 57.3%)

Chunk Details:
  Chunk#  Offset      UncompSize    CompSize      Records   Dropped   CRC32     
  ----------------------------------------------------------------------
  0       128         840           359           10        0         OK
============================================================
```

### Step 3: Dump Chronological Event Stream (`ubpftrace-cat --dump`)
```bash
./bin/ubpftrace-cat --dump examples/getting_started/*.ubpf
```

**Real Output:**
```text
[114217.709356s] [Node 3650575891] [Rank 0] [Event 1] [TS 114217681441997 ns] Task 0 processed data_size=10 bytes

[114217.783379s] [Node 3650575891] [Rank 0] [Event 1] [TS 114217781527321 ns] Task 1 processed data_size=27 bytes

[114217.882765s] [Node 3650575891] [Rank 0] [Event 1] [TS 114217881603528 ns] Task 2 processed data_size=44 bytes

[114217.983148s] [Node 3650575891] [Rank 0] [Event 1] [TS 114217981679604 ns] Task 3 processed data_size=61 bytes

[114218.083583s] [Node 3650575891] [Rank 0] [Event 1] [TS 114218081744611 ns] Task 4 processed data_size=78 bytes

[114218.182912s] [Node 3650575891] [Rank 0] [Event 1] [TS 114218181821639 ns] Task 5 processed data_size=95 bytes

[114218.283432s] [Node 3650575891] [Rank 0] [Event 1] [TS 114218281898567 ns] Task 6 processed data_size=12 bytes

[114218.383205s] [Node 3650575891] [Rank 0] [Event 1] [TS 114218381973682 ns] Task 7 processed data_size=29 bytes

[114218.483654s] [Node 3650575891] [Rank 0] [Event 1] [TS 114218482054056 ns] Task 8 processed data_size=46 bytes

[114218.584114s] [Node 3650575891] [Rank 0] [Event 1] [TS 114218582136084 ns] Task 9 processed data_size=63 bytes
```

### Step 4: Export to Google Chrome Tracing / Perfetto Format
```bash
./bin/ubpftrace-cat --chrome timeline.json examples/getting_started/*.ubpf
```

**Real Output:**
```text
Exported Chrome Trace Event format to: timeline.json
```
Open **[ui.perfetto.dev](https://ui.perfetto.dev)** in your browser and open `timeline.json` to view interactive Gantt charts and latency timelines.

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
