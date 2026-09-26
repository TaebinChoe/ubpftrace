# ubpftrace Companion Toolchains (`tools/`)

This directory contains the implementations of the three standalone companion tools packaged with `ubpftrace`:

---

## Tool Implementations

| Source File | Executable Target | Purpose & Capabilities |
| :--- | :--- | :--- |
| **[`ubpftrace_attach.cpp`](ubpftrace_attach.cpp)** | **`bin/ubt-attach`** | Dynamic multi-node runtime injector and live probe hot-patcher for unmodified running HPC / MPI jobs. |
| **[`ubpftrace_top.cpp`](ubpftrace_top.cpp)** | **`bin/ubt-top`** | Real-time ANSI TUI cluster monitoring dashboard, JSON telemetry streamer, and straggler node detector. |
| **[`ubpftrace_cat.cpp`](ubpftrace_cat.cpp)** | **`bin/ubt-cat`** | High-throughput binary `.ubt` / `.ubpt` trace container decoder, multi-node min-heap merger, and Perfetto/Chrome visualizer exporter. |

---

## Quick Reference

### 1. Dynamic Runtime Injection (`ubt-attach`)
```bash
# Attach to running process by PID:
./bin/ubt-attach -p <PID> -s script.bt

# Fan-out dynamic injection across all nodes in a Slurm job:
./bin/ubt-attach --job <JOBID> --comm <APP_NAME> -s script.bt

# Safely detach probes from all ranks across the cluster (0 overhead):
./bin/ubt-attach --job <JOBID> --comm <APP_NAME> -d
```

### 2. Live Cluster Dashboard (`ubt-top`)
```bash
# Monitor live telemetry from active Slurm job:
./bin/ubt-top -j <JOBID>

# Single-shot cluster reduction:
./bin/ubt-top -j <JOBID> --once
```

### 3. Trace Container Decoder & Merged Timeline (`ubt-cat`)
```bash
# Inspect container metadata & LZ4 compression ratio:
./bin/ubt-cat -j <JOBID> -i

# Merge multi-node containers and dump chronological event stream:
./bin/ubt-cat -j <JOBID> -m -d

# Export merged timeline to Perfetto / Chrome Tracing format:
./bin/ubt-cat -j <JOBID> -m --chrome timeline.json
```

For complete user manuals and advanced workflows, see **[Companion Toolchains Manual](../docs/toolchains.md)**.
