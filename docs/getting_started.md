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
- **`bin/ubt-top`**: Real-time TUI cluster monitoring dashboard (`ubpftrace-top` alias).
- **`bin/ubt-cat`**: Standalone container reader, decoder, and Perfetto/Chrome-trace exporter (`ubpftrace-cat` alias).

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

## 6. Scenario 4: Live Cluster Dashboard with `ubt-top`

`ubpftrace` can export lock-free map snapshots periodically out-of-band to a shared directory. `ubt-top` reads these snapshots and renders a cluster-wide aggregated TUI dashboard without interrupting target processes.

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

### Step 2: Open Terminal 2 and Launch `ubt-top`
```bash
./bin/ubt-top -d ./examples/getting_started/.live_demo
```

**Real `ubt-top` Dashboard Output:**
```text
================================================================================
 ubt-top :: Real-Time Cluster Aggregation Dashboard (Cycle #1)
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

## 7. Scenario 5: High-Speed Trace Containers (`.ubt`) & `ubt-cat`

When detailed event traces are required, `ubpftrace` offloads events to an unpinned ring buffer. A dedicated background I/O worker compresses chunks with LZ4 and writes them into high-performance `.ubt` containers.

### Script: [`05_trace_container_cat.bt`](../examples/getting_started/05_trace_container_cat.bt)
```bt
uprobe:./examples/getting_started/target_app:compute_task {
    printf("[TS %lu ns] Task %d processed data_size=%d bytes\n",
           nsecs, arg0, arg1);
}
```

### Step 1: Record Traces to `.ubt` Container
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

### Step 2: Inspect Container Metadata & LZ4 Compression (`ubt-cat --info`)
```bash
./bin/ubt-cat --info examples/getting_started/*.ubt
```

**Real Output:**
```text
============================================================
  UBPFTRACE CONTAINER METADATA: examples/getting_started/ubpftrace_1057033_node_3650575891.ubt
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

### Step 3: Dump Chronological Event Stream (`ubt-cat --dump`)
```bash
./bin/ubt-cat --dump examples/getting_started/*.ubt
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
./bin/ubt-cat --chrome timeline.json examples/getting_started/*.ubt
```

**Real Output:**
```text
Exported Chrome Trace Event format to: timeline.json
```
Open **[ui.perfetto.dev](https://ui.perfetto.dev)** in your browser and open `timeline.json` to view interactive Gantt charts and latency timelines.

---

### 8. Scenario 6: Dynamic Runtime Injection & Hot-Patching on Running HPC Clusters (`ubt-attach`)

In production HPC environments, large-scale simulations may run for days across hundreds of nodes. Restarting workloads to add profiling wrappers or recompiling binaries with instrumentation is often prohibitive. `ubpftrace` solves this with **zero-restart dynamic runtime injection**:

1. **Unmodified Process Injection**: Probes are attached into live, running processes via `ptrace`/Frida without restarting or preloading libraries.
2. **Lock-Free In-Memory Aggregations**: Telemetry maps accumulate in shared memory (`/dev/shm`) with zero disk I/O interference.
3. **Dynamic Detachment (0 Overhead)**: When diagnostic capture completes, probes are cleanly unhooked at runtime while target workloads continue at native speed.
4. **Hot-Patching / Re-Attachment**: New or modified `.bt` probe scripts can be hot-patched into the running processes via fast IPC Unix domain sockets without re-injecting the agent.

---

### Step 1: Launch the Unmodified HPC Application

All nodes execute [`examples/apps/hpc_app.c`](../examples/apps/hpc_app.c), a multi-rank MPI simulation featuring halo exchanges, imbalanced grid physics (`simulate_grid_computation`), barrier synchronization (`MPI_Barrier`), and global reductions (`MPI_Allreduce`):

```bash
# Launch across 2 compute nodes (4 ranks total) in continuous mode (0)
srun -N 2 --ntasks-per-node=2 ./examples/apps/hpc_app 0 > /pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/hpc_app.log 2>&1 &
```

**Verify Target Liveness Across Nodes:**
```bash
srun --overlap -N 2 --ntasks-per-node=1 bash -c 'hostname; pgrep -a hpc_app'
```

**Real Output:**
```text
nid004155
68092 /pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/examples/apps/hpc_app 0
68093 /pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/examples/apps/hpc_app 0
nid004156
1026218 /pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/examples/apps/hpc_app 0
1026219 /pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/examples/apps/hpc_app 0
```

---

### Step 2: Verify Initial Cluster State (`ubt-top` Waiting)

Before attaching any probes, verify that `ubt-top` reflects a clean, uninstrumented state:

```bash
./bin/ubt-top -j 58893883 --once
```

**Real Output:**
```text
================================================================================
 ubt-top :: Real-Time Cluster Aggregation Dashboard (Cycle #1)
================================================================================
 Snapshot Dir : /pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/traces/.ubpftrace_live_58893883
 Active Nodes : 0 | Stragglers: 0 | Interval: 1s
--------------------------------------------------------------------------------

  [Waiting for node snapshots in /pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/traces/.ubpftrace_live_58893883...]

[Press Ctrl+C to stop monitoring]
```

---

### Step 3: Dynamic Multi-Node Injection with `ubt-attach`

Target script [`examples/apps/trace_hpc.bt`](../examples/apps/trace_hpc.bt) instruments grid computation duration, barrier stall times, and reduction latencies:

```bt
// examples/apps/trace_hpc.bt
uprobe:/pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/examples/apps/hpc_app:simulate_grid_computation {
    $rank = arg0;
    $iter = arg1;
    @comp_start[tid] = nsecs;
    @compute_calls[$rank] = count();
    @grid_step_total[$rank] = count();
    @iter_min[$rank] = min($iter);
    @iter_max[$rank] = max($iter);
    @grid_skew_score = max($iter);
}

uprobe:/opt/cray/pe/lib64/libmpi_gnu.so:MPI_Barrier {
    if (@comp_start[tid]) {
        $comp_dur_us = (nsecs - @comp_start[tid]) / 1000;
        printf("[HPC Compute] Compute time: %u us\n", $comp_dur_us);
        @comp_dur_min = min($comp_dur_us);
        @comp_dur_max = max($comp_dur_us);
        @comp_dur_stats = stats($comp_dur_us);
        @comp_dur_hist = hist($comp_dur_us);
        _ = delete(@comp_start, tid);
    }
    @barrier_start[tid] = nsecs;
    @barrier_calls = count();
}

uretprobe:/opt/cray/pe/lib64/libmpi_gnu.so:MPI_Barrier /@barrier_start[tid]/ {
    $barrier_dur_us = (nsecs - @barrier_start[tid]) / 1000;
    printf("[MPI Barrier] Barrier wait time: %u us\n", $barrier_dur_us);
    @barrier_min_us = min($barrier_dur_us);
    @barrier_max_us = max($barrier_dur_us);
    @barrier_stats_us = stats($barrier_dur_us);
    @barrier_latency_hist = hist($barrier_dur_us);
    _ = delete(@barrier_start, tid);
}
```

**Attach to All Ranks Across Cluster Nodes:**
```bash
srun --overlap -N 2 --ntasks-per-node=1 ./bin/ubt-attach --comm hpc_app -s ./examples/apps/trace_hpc.bt
```

**Real Output:**
```text
╔════════════════════════════════════════════════════════════════════╗
║       ubt-attach: Dynamic Multi-Node Runtime Injector              ║
╚════════════════════════════════════════════════════════════════════╝
Node Context: nid004155
╔════════════════════════════════════════════════════════════════════╗
║       ubt-attach: Dynamic Multi-Node Runtime Injector              ║
╚════════════════════════════════════════════════════════════════════╝
Node Context: nid004156
  [COMPILE-PROBES] Compiling trace_hpc.bt to eBPF manifest... SUCCESS
  [COMPILE-PROBES] Compiling trace_hpc.bt to eBPF manifest... SUCCESS
  [ACTIVATE-SHM] Activating eBPF runtime into persistent SHM... SUCCESS
  [DISCOVER] Found 2 local processes matching 'hpc_app': [68092, 68093]
  [ACTIVATE-SHM] Activating eBPF runtime into persistent SHM... SUCCESS
  [DISCOVER] Found 2 local processes matching 'hpc_app': [1026218, 1026219]
  [INJECT] Injecting libbpftime-agent.so into PID 68092... SUCCESS
  [INJECT] Injecting libbpftime-agent.so into PID 1026218... SUCCESS
  [INJECT] Injecting libbpftime-agent.so into PID 68093... SUCCESS
  [INJECT] Injecting libbpftime-agent.so into PID 1026219... SUCCESS
Summary: Injected into 2/2 processes successfully on nid004155.
Summary: Injected into 2/2 processes successfully on nid004156.
```

---

### Step 4: Stream Real-Time Cluster Aggregations with `ubt-top`

Inspect the live cluster reduction dashboard:
```bash
./bin/ubt-top -j 58893883 --once
```

**Real Dashboard Output:**
```text
================================================================================                                      
 ubt-top :: Real-Time Cluster Aggregation Dashboard (Cycle #1)
================================================================================
 Snapshot Dir : /pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/traces/.ubpftrace_live_58893883
 Active Nodes : 2 | Stragglers: 0 | Interval: 1s
--------------------------------------------------------------------------------

[CLUSTER-WIDE METRIC AGGREGATIONS]
Map Name                        Global Max    Global Min      Global Sum Entries
--------------------------------------------------------------------------------
@allreduce_ca                          984           984            1968       2
@allreduce_ma                        43183         40234           83417       2
@allreduce_mi                          240            60             300       2
@allreduce_st                       471494        188111          659605       2
@barrier_call                          984           984            1968       2
@barrier_late                          968             1            1965      15
@barrier_max_                       292188         72074          364262       2
@barrier_min_                       140396         37981          178377       2
@barrier_star               316855604914557309996132983790 936847871070011       3
@barrier_stat                     23120398      11860102        34980500       2
@comp_dur_his                          967             1            1967       8
@comp_dur_max                       101389         80089          181478       2
@comp_dur_min                        55967         23211           79178       2
@comp_dur_sta                     14448029       3061178        17509207       2
@comp_start                 316855602102507316855602102507 316855602102507       1
@compute_call                          498           486            1968       4
@grid_skew_sc                        21238          8932           30170       2
@grid_step_to                          498           486            1968       4
@iter_max                            10751          4368           30170       4
@iter_min                            10158          3805           27734       4
@reduction_ra                          498           486            1968       4

[HISTOGRAM: @compute_call]
           0 : [=========================] 498
           1 : [======================== ] 486
           2 : [=========================] 498
           3 : [======================== ] 486

[HISTOGRAM: @iter_max]
           0 : [==========               ] 4564
           1 : [==========               ] 4368
           2 : [======================== ] 10487
           3 : [=========================] 10751

[HISTOGRAM: @iter_min]
           0 : [=========                ] 3805
           1 : [=========                ] 3848
           2 : [======================== ] 9923
           3 : [=========================] 10158

[NODE TOPOLOGY & SYNC STATUS]
Node ID   Hostname            Epoch     Latency Lag (ms)    Status         
---------------------------------------------------------------------------
0         nid004155           74        0.00                [HEALTHY]
1         nid004156           74        0.87                [HEALTHY]

[Press Ctrl+C to stop monitoring]
```

---

### Step 5: Zero-Overhead Dynamic Detachment (`ubt-attach -d`)

When tracing is complete, detach probes instantly across all nodes without interrupting or restarting `hpc_app`:

```bash
srun --overlap -N 2 --ntasks-per-node=1 ./bin/ubt-attach --comm hpc_app -d
```

**Real Output:**
```text
╔════════════════════════════════════════════════════════════════════╗
║       ubt-attach: Dynamic Multi-Node Runtime Injector              ║
╚════════════════════════════════════════════════════════════════════╝
Node Context: nid004155
╔════════════════════════════════════════════════════════════════════╗
║       ubt-attach: Dynamic Multi-Node Runtime Injector              ║
╚════════════════════════════════════════════════════════════════════╝
Node Context: nid004156
  [DISCOVER] Found 2 local processes matching 'hpc_app': [68092, 68093]
  [DISCOVER] Found 2 local processes matching 'hpc_app': [1026218, 1026219]
  [DETACH] Detaching probes from PID 68092... SUCCESS (ok)
  [DETACH] Detaching probes from PID 68093... SUCCESS (ok)
Summary: Detached from 2/2 processes successfully on nid004155.
  [DETACH] Detaching probes from PID 1026218... SUCCESS (ok)
  [DETACH] Detaching probes from PID 1026219... SUCCESS (ok)
Summary: Detached from 2/2 processes successfully on nid004156.
```

---

### Step 6: Verify Target Workload Uninterrupted Native Execution

Confirm that all ranks continue executing at full native speed with zero residual tracing overhead:

```bash
srun --overlap -N 2 --ntasks-per-node=1 bash -c 'hostname; pgrep -a hpc_app'
```

**Real Output:**
```text
nid004155
68092 /pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/examples/apps/hpc_app 0
68093 /pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/examples/apps/hpc_app 0
nid004156
1026218 /pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/examples/apps/hpc_app 0
1026219 /pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/examples/apps/hpc_app 0
```

---

### Step 7: Hot-Patching & Re-Attachment via IPC Socket Refresh (`[REFRESH]`)

To re-attach or hot-patch a modified probe script with new telemetry maps, run `ubt-attach` again:

```bash
srun --overlap -N 2 --ntasks-per-node=1 ./bin/ubt-attach --comm hpc_app -s ./examples/apps/trace_hpc.bt
```

**Real Output:**
```text
╔════════════════════════════════════════════════════════════════════╗
║       ubt-attach: Dynamic Multi-Node Runtime Injector              ║
╚════════════════════════════════════════════════════════════════════╝
Node Context: nid004155
╔════════════════════════════════════════════════════════════════════╗
║       ubt-attach: Dynamic Multi-Node Runtime Injector              ║
╚════════════════════════════════════════════════════════════════════╝
Node Context: nid004156
  [COMPILE-PROBES] Compiling trace_hpc.bt to eBPF manifest... SUCCESS
  [COMPILE-PROBES] Compiling trace_hpc.bt to eBPF manifest... SUCCESS
  [ACTIVATE-SHM] Activating eBPF runtime into persistent SHM... SUCCESS
  [DISCOVER] Found 2 local processes matching 'hpc_app': [68092, 68093]
  [ACTIVATE-SHM] Activating eBPF runtime into persistent SHM... SUCCESS
  [DISCOVER] Found 2 local processes matching 'hpc_app': [1026218, 1026219]
  [REFRESH] Re-attached & refreshed probes in PID 68092... SUCCESS
  [REFRESH] Re-attached & refreshed probes in PID 68093... SUCCESS
Summary: Injected into 2/2 processes successfully on nid004155.
  [REFRESH] Re-attached & refreshed probes in PID 1026218... SUCCESS
  [REFRESH] Re-attached & refreshed probes in PID 1026219... SUCCESS
Summary: Injected into 2/2 processes successfully on nid004156.
```
> [!TIP]
> Notice the **`[REFRESH]`** step: `ubt-attach` detects the resident agent and instantly swaps probe links over Unix domain sockets with zero ptrace latency.

---

### Step 8: Inspect Multi-Node Trace Containers (`ubt-cat -i`)

Verify chunk-level CRC32 integrity and LZ4 compression across nodes:

```bash
./bin/ubt-cat -j 58893883 -i
```

**Real Output:**
```text
============================================================
  UBPFTRACE CONTAINER METADATA: traces/ubpftrace_58893883_node_1.ubpt
============================================================
  Job ID            : 58893883
  Node ID           : 1
  Hostname          : nid004156
  File Size         : 105220 bytes (0.10 MB)
  Codec             : LZ4
  Base Monotonic Ts : 309981099570570 ns
  Base Wallclock Ts : 1790427244421374401 ns
  Total Chunks      : 73
  Total Records     : 7037
  Total Dropped     : 0
  Uncompressed Size : 281480 bytes
  Compressed Size   : 100420 bytes (Savings: 64.3%)

Chunk Details:
  Chunk#  Offset      UncompSize    CompSize      Records   Dropped   CRC32     
  ----------------------------------------------------------------------
  0       128         2560          937           64        0         OK
  1       1129        4080          1463          166       0         OK
  ...
  72      103717      4080          1439          7037      0         OK
============================================================
```

---

### Step 9: Dump Chronologically Merged Multi-Node Event Stream (`ubt-cat -m -d`)

Merge trace containers across all nodes into a unified chronological stream:

```bash
./bin/ubt-cat -j 58893883 -m -d | head -n 15
```

**Real Output:**
```text
[309981.102394s] [Node 1] [Rank 2] [Event 1] [MPI Allreduce] Allreduce latency: 18674 us
[309981.105961s] [Node 1] [Rank 2] [Event 1] [HPC Compute] Compute time: 2555 us
[309981.129544s] [Node 1] [Rank 2] [Event 1] [MPI Barrier] Barrier wait time: 23535 us
[309981.130645s] [Node 1] [Rank 2] [Event 1] [MPI Allreduce] Allreduce latency: 7 us
[309981.133710s] [Node 1] [Rank 2] [Event 1] [HPC Compute] Compute time: 2415 us
[309981.157580s] [Node 1] [Rank 2] [Event 1] [MPI Barrier] Barrier wait time: 23868 us
[309981.158709s] [Node 1] [Rank 2] [Event 1] [MPI Allreduce] Allreduce latency: 6 us
[309981.161750s] [Node 1] [Rank 2] [Event 1] [HPC Compute] Compute time: 2373 us
[309981.185739s] [Node 1] [Rank 2] [Event 1] [MPI Barrier] Barrier wait time: 23987 us
[309981.186842s] [Node 1] [Rank 2] [Event 1] [MPI Allreduce] Allreduce latency: 6 us
[309981.190054s] [Node 1] [Rank 2] [Event 1] [HPC Compute] Compute time: 2478 us
[309981.214221s] [Node 1] [Rank 2] [Event 1] [MPI Barrier] Barrier wait time: 24165 us
[309981.215415s] [Node 1] [Rank 2] [Event 1] [MPI Allreduce] Allreduce latency: 7 us
[309981.218598s] [Node 1] [Rank 2] [Event 1] [HPC Compute] Compute time: 2369 us
[309981.242537s] [Node 1] [Rank 2] [Event 1] [MPI Barrier] Barrier wait time: 23937 us
```

---

## 9. Summary & Next Steps

| Workload Requirement | Recommended Approach | Tooling |
| :--- | :--- | :--- |
| **Simple Call & Arg Inspection** | Function tracing with `printf()` | `ubpftrace -c ...` |
| **High-Frequency Statistics** | In-memory lock-free BPF maps (`count()`, `hist()`, `stats()`) | `ubpftrace` (summarized at exit) |
| **Periodic / Bounded Metrics** | Time-window epoch keys (`elapsed / 1s`) | `ubpftrace` |
| **Cluster-Wide Online Telemetry** | Periodic JSON snapshots (`--live-ms 300`) | `ubt-top` |
| **Zero-Restart Live Job Injection** | Dynamic ptrace injection & IPC refresh | `ubt-attach` + `ubt-top` |
| **High-Volume Timeline Analysis** | LZ4 container offloading (`.ubpt`) & merged stream | `ubt-cat` + Perfetto |

- Ready to profile multi-node MPI applications and GPU workloads? Check out the **[Production Presets Catalog](../presets/README.md)**.
- For MPI/Slurm environment setup and Lustre parallel filesystem tuning, see the **[HPC & MPI Guide](hpc_and_mpi_guide.md)**.

