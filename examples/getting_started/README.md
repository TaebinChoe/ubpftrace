# ubpftrace Getting Started Tutorial & Examples

This directory contains minimal, self-contained examples demonstrating the five primary usage patterns of `ubpftrace`.

---

## Directory Contents

| File | Purpose | Key Concepts |
| :--- | :--- | :--- |
| [`target_app.c`](target_app.c) | C workload target simulating task processing | `compute_task(task_id, data_size)` |
| [`01_function_tracing.bt`](01_function_tracing.bt) | Function Tracing & Argument Extraction | `uprobe`, `arg0`, `arg1`, `printf()`, `nsecs` |
| [`02_map_aggregation.bt`](02_map_aggregation.bt) | In-Memory BPF Map Aggregation | `count()`, `stats()`, `hist()`, zero I/O overhead |
| [`03_windowed_metrics.bt`](03_windowed_metrics.bt) | Periodic Time-Window Aggregations | `$window = elapsed / 1s`, rate-limited `printf()` |
| [`04_live_top_dashboard.bt`](04_live_top_dashboard.bt) | Real-Time Cluster Monitoring with `ubpftrace-top` | `--live-ms 300`, JSON out-of-band snapshots |
| [`05_trace_container_cat.bt`](05_trace_container_cat.bt) | High-Speed Container Recording & `ubpftrace-cat` | `.ubpf` container, LZ4 compression, Chrome tracing |
| [`run.sh`](run.sh) | Automated test runner executing all 5 scenarios | End-to-end verification |

---

## 1. Build the Target Application

```bash
make
```

This compiles `target_app` with standard debug symbols (`-g -fno-inline -fno-omit-frame-pointer`).

---

## 2. Running the 5 Scenarios

### Scenario 1: Function Entry & Argument Tracing
Trace individual function calls in real time and inspect passed arguments:
```bash
./bin/ubpftrace -c "./examples/getting_started/target_app 10" examples/getting_started/01_function_tracing.bt
```

### Scenario 2: Zero-Overhead In-Memory Map Aggregations
Compute global counts, average data sizes, and power-of-2 logarithmic histograms directly in shared memory without disk I/O:
```bash
./bin/ubpftrace -c "./examples/getting_started/target_app 25" examples/getting_started/02_map_aggregation.bt
```

### Scenario 3: Periodic Time-Window Bucketing & Throttled Stream
Bucket metrics into 1-second epochs (`$window_sec = elapsed / 1000000000`) and emit a throttled live stream notification once per window:
```bash
./bin/ubpftrace --no-warnings -c "./examples/getting_started/target_app 30" examples/getting_started/03_windowed_metrics.bt
```

### Scenario 4: Live Cluster Dashboard (`ubpftrace-top`)
Monitor running metrics online in an interactive TUI dashboard:
```bash
# Terminal 1: Launch application with periodic JSON snapshots
UBPFTRACE_LIVE_DIR=./examples/getting_started/.live_demo ./bin/ubpftrace --live-ms 300 -c "./examples/getting_started/target_app 30" examples/getting_started/04_live_top_dashboard.bt

# Terminal 2: Connect real-time monitoring dashboard
./bin/ubpftrace-top -d ./examples/getting_started/.live_demo
```

### Scenario 5: High-Speed Trace Containers (`.ubpf`) & `ubpftrace-cat`
Offload high-volume event streams to LZ4-compressed `.ubpf` container files with background I/O workers, then inspect post-mortem:
```bash
# Record trace container
UBPFTRACE_OUTPUT_DIR=./examples/getting_started ./bin/ubpftrace -c "./examples/getting_started/target_app 15" examples/getting_started/05_trace_container_cat.bt

# Inspect container metadata & compression savings
./bin/ubpftrace-cat --info examples/getting_started/*.ubpf

# Dump chronological event stream
./bin/ubpftrace-cat --dump examples/getting_started/*.ubpf

# Export to Chrome Tracing / Perfetto JSON
./bin/ubpftrace-cat --chrome timeline.json examples/getting_started/*.ubpf
```

---

## 3. Run All Scenarios Automatically

You can run the full suite at any time:
```bash
./examples/getting_started/run.sh
```
