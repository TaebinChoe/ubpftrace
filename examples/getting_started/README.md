# ubpftrace Getting Started Tutorial & Examples

This directory contains minimal, self-contained examples demonstrating the five primary usage patterns of `ubpftrace`. Every file contains full, reproducible terminal prompts and expected output.

---

## Directory Contents

| File | Purpose | Key Concepts & Tooling |
| :--- | :--- | :--- |
| [`target_app.c`](target_app.c) | C workload target simulating task processing | `compute_task(task_id, data_size)` |
| [`Makefile`](Makefile) | Compilation target | Standard GCC debug build |
| [`01_function_tracing.bt`](01_function_tracing.bt) | Function Tracing & Argument Extraction | `uprobe`, `arg0`, `arg1`, `printf()`, `nsecs` |
| [`02_map_aggregation.bt`](02_map_aggregation.bt) | In-Memory BPF Map Aggregation | `count()`, `stats()`, `hist()`, zero I/O overhead |
| [`03_windowed_metrics.bt`](03_windowed_metrics.bt) | Periodic Time-Window Aggregations | `$window = elapsed / 1s`, rate-limited `printf()` |
| [`04_live_top_dashboard.bt`](04_live_top_dashboard.bt) | Real-Time Cluster Monitoring | `ubpftrace-top`, `--live-ms 300`, JSON snapshots |
| [`05_trace_container_cat.bt`](05_trace_container_cat.bt) | High-Speed Trace Containers | `ubpftrace-cat`, `.ubpf` containers, LZ4 compression |
| [`run.sh`](run.sh) | Automated test runner | Executes all 5 scenarios end-to-end |

---

## 1. Build the Target Application

```bash
$ make -C examples/getting_started
```

This compiles `target_app` with standard debug symbols (`-g -fno-inline -fno-omit-frame-pointer`).

---

## 2. Step-by-Step Scenario Prompts

### Scenario 1: Function Entry & Argument Tracing
Trace individual function calls in real time and inspect passed arguments:
```bash
$ ./bin/ubpftrace -c "./examples/getting_started/target_app 5" examples/getting_started/01_function_tracing.bt
```
**Expected Output:**
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

### Scenario 2: Zero-Overhead In-Memory Map Aggregations
Compute global counts, average data sizes, and power-of-2 logarithmic histograms directly in shared memory without disk I/O:
```bash
$ ./bin/ubpftrace -c "./examples/getting_started/target_app 20" examples/getting_started/02_map_aggregation.bt
```
**Expected Output:**
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

### Scenario 3: Periodic Time-Window Bucketing & Throttled Stream
Bucket metrics into 1-second epochs (`$window_sec = elapsed / 1000000000`) and emit a throttled live stream notification once per window:
```bash
$ ./bin/ubpftrace --no-warnings -c "./examples/getting_started/target_app 25" examples/getting_started/03_windowed_metrics.bt
```
**Expected Output:**
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

### Scenario 4: Live Cluster Dashboard with `ubpftrace-top`
Monitor running metrics online in an interactive TUI dashboard:

**Terminal 1 (Launch Application with Periodic Live Snapshots):**
```bash
$ UBPFTRACE_LIVE_DIR=./examples/getting_started/.live_demo \
  ./bin/ubpftrace --live-ms 300 -c "./examples/getting_started/target_app 30" \
  examples/getting_started/04_live_top_dashboard.bt
```

**Terminal 2 (Launch `ubpftrace-top` Dashboard):**
```bash
$ ./bin/ubpftrace-top -d ./examples/getting_started/.live_demo
```

**Expected `ubpftrace-top` TUI Screen:**
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

### Scenario 5: High-Speed Trace Containers (`.ubpf`) & `ubpftrace-cat`
Offload high-volume event streams to LZ4-compressed `.ubpf` container files with background I/O workers, then inspect post-mortem:

**Step 1: Record Trace to Container:**
```bash
$ UBPFTRACE_OUTPUT_DIR=./examples/getting_started \
  ./bin/ubpftrace -c "./examples/getting_started/target_app 10" \
  examples/getting_started/05_trace_container_cat.bt
```

**Step 2: Inspect Container Metadata & LZ4 Compression:**
```bash
$ ./bin/ubpftrace-cat --info examples/getting_started/*.ubpf
```
```text
============================================================
  UBPFTRACE CONTAINER METADATA: ubpftrace_1014833_node_3650575891.ubpf
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
============================================================
```

**Step 3: Dump Chronological Event Stream:**
```bash
$ ./bin/ubpftrace-cat --dump examples/getting_started/*.ubpf
```
```text
[113086.565397s] [Node 3650575891] [Rank 0] [Event 1] [TS 113086538302792 ns] Task 0 processed data_size=10 bytes
[113086.639523s] [Node 3650575891] [Rank 0] [Event 1] [TS 113086638374488 ns] Task 1 processed data_size=27 bytes
[113086.739971s] [Node 3650575891] [Rank 0] [Event 1] [TS 113086738434281 ns] Task 2 processed data_size=44 bytes
[113086.840313s] [Node 3650575891] [Rank 0] [Event 1] [TS 113086838493513 ns] Task 3 processed data_size=61 bytes
[113086.939643s] [Node 3650575891] [Rank 0] [Event 1] [TS 113086938548448 ns] Task 4 processed data_size=78 bytes
```

**Step 4: Export to Google Chrome Tracing / Perfetto Format:**
```bash
$ ./bin/ubpftrace-cat --chrome timeline.json examples/getting_started/*.ubpf
```
Open **[ui.perfetto.dev](https://ui.perfetto.dev)** in your browser and open `timeline.json`.

---

## 3. Automated End-to-End Test Suite

To run all 5 scenarios automatically:
```bash
$ ./examples/getting_started/run.sh
```
