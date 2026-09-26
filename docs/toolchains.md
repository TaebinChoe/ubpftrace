# Companion Toolchains Manual: ubt-attach, ubt-cat & ubt-top

`ubpftrace` includes three standalone companion utilities engineered for dynamic runtime injection, live cluster operations, and high-throughput offline decoding:

1. **`ubt-attach`**: Dynamic multi-node runtime injector and live probe hot-patcher for running HPC jobs.
2. **`ubt-cat`**: Binary `.ubt` / `.ubpt` trace container decoder, multi-node timeline merger, and Perfetto/Chrome visualizer exporter.
3. **`ubt-top`**: Interactive real-time cluster TUI dashboard, snapshot aggregator, and automated straggler detector.

---

## Table of Contents
1. [ubt-attach: Dynamic Multi-Node Runtime Injector & Hot-Patcher](#1-ubt-attach-dynamic-multi-node-runtime-injector--hot-patcher)
   - [Overview & CLI Reference](#overview--cli-reference-1)
   - [Single-Process & Local Multi-Rank Injection](#single-process--local-multi-rank-injection)
   - [Cluster-Wide Slurm Fan-Out Orchestration](#cluster-wide-slurm-fan-out-orchestration)
   - [Live Hot-Patching via IPC Socket Refresh ([REFRESH])](#live-hot-patching-via-ipc-socket-refresh-refresh)
   - [Zero-Overhead Dynamic Detachment (-d)](#zero-overhead-dynamic-detachment--d)
2. [ubt-top: Real-Time Cluster TUI & Straggler Dashboard](#2-ubt-top-real-time-cluster-tui--straggler-dashboard)
   - [Overview & CLI Reference](#overview--cli-reference-2)
   - [Interactive Terminal Dashboard](#interactive-terminal-dashboard)
   - [Automated Straggler Node Detection](#automated-straggler-node-detection)
   - [Single-Shot Mode & Machine-Readable JSON Streaming](#single-shot-mode--machine-readable-json-streaming)
3. [ubt-cat: Trace Container Decoder & Perfetto Exporter](#3-ubt-cat-trace-container-decoder--perfetto-exporter)
   - [Overview & CLI Reference](#overview--cli-reference-3)
   - [Inspecting Container Metadata & Compression (--info)](#inspecting-container-metadata--compression---info)
   - [Dumping Chronological Event Logs (--dump)](#dumping-chronological-event-logs---dump)
   - [Multi-Node K-Way Min-Heap Merge (--merge)](#multi-node-k-way-min-heap-merge---merge)
   - [Exporting to Perfetto / Chrome Tracing (--chrome)](#exporting-to-perfetto--chrome-tracing---chrome)

---

## 1. ubt-attach: Dynamic Multi-Node Runtime Injector & Hot-Patcher

### Overview & CLI Reference

In production HPC environments, restarting long-running simulation jobs or recompiling applications with instrumentation wrappers is often impractical. `ubt-attach` provides **zero-restart dynamic runtime injection**:

- Probes are injected into live, running processes via `ptrace`/Frida.
- Telemetry maps and event buffers allocate lock-free in POSIX shared memory (`/dev/shm`).
- Probes can be detached dynamically (`-d`), restoring native execution at 0 overhead.
- Resident agents listen on Unix domain sockets, enabling instant hot-patching (`[REFRESH]`) when script definitions change.

```text
Usage: ubt-attach [OPTIONS]

Options:
  -p, --pid <PID>         Target single process ID on the local node
  -c, --comm <NAME>       Target all local processes matching executable name
  -s, --script <PATH>     Compile and attach .bt tracing script into targets
  -j, --job <JOBID>       Fan-out attach across all nodes in Slurm job
  -w, --node <NODELIST>   Target specific node or nodelist
  -d, --detach            Send detach signal to unhook probes cleanly
  -a, --agent-so <PATH>   Path to libbpftime-agent.so (auto-discovered if omitted)
  -h, --help              Show this help message
```

---

### Single-Process & Local Multi-Rank Injection

Attach to a single PID or all processes matching an executable name on the local host:

```bash
# Target a specific PID:
./bin/ubt-attach -p 68092 -s ./examples/apps/trace_hpc.bt

# Target all local processes matching executable name:
./bin/ubt-attach --comm hpc_app -s ./examples/apps/trace_hpc.bt
```

---

### Cluster-Wide Slurm Fan-Out Orchestration

When invoked with `--job <JOBID>`, `ubt-attach` discovers all compute nodes in the active Slurm allocation and dispatches parallel injection tasks across all nodes via `srun --overlap`:

```bash
# Fan-out injection across all compute nodes in Slurm job 58893883:
./bin/ubt-attach --job 58893883 --comm hpc_app -s ./examples/apps/trace_hpc.bt
```

**Real Output**:
```text
╔════════════════════════════════════════════════════════════════════╗
║       ubt-attach: Dynamic Multi-Node Runtime Injector              ║
╚════════════════════════════════════════════════════════════════════╝
>>> Initiating Remote Orchestration for Slurm Job: 58893883 (All Compute Nodes)
  [REMOTE] Executing: srun --jobid=58893883 --overlap -N 2 --ntasks-per-node=1 ...

Node Context: nid004155
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

### Live Hot-Patching via IPC Socket Refresh (`[REFRESH]`)

When modifying a `.bt` script to add new metrics, running `ubt-attach` again detects the resident agent in target processes and hot-patches the probes via Unix domain sockets in $< 10\text{ ms}$:

```bash
./bin/ubt-attach --job 58893883 --comm hpc_app -s ./examples/apps/trace_hpc.bt
```

**Output**:
```text
  [REFRESH] Re-attached & refreshed probes in PID 68092... SUCCESS
  [REFRESH] Re-attached & refreshed probes in PID 68093... SUCCESS
  [REFRESH] Re-attached & refreshed probes in PID 1026218... SUCCESS
  [REFRESH] Re-attached & refreshed probes in PID 1026219... SUCCESS
```

---

### Zero-Overhead Dynamic Detachment (`-d`)

To unhook all probes and return target workloads to 100% native execution speed without restarting:

```bash
./bin/ubt-attach --job 58893883 --comm hpc_app -d
```

**Output**:
```text
  [DETACH] Detaching probes from PID 68092... SUCCESS (ok)
  [DETACH] Detaching probes from PID 68093... SUCCESS (ok)
  [DETACH] Detaching probes from PID 1026218... SUCCESS (ok)
  [DETACH] Detaching probes from PID 1026219... SUCCESS (ok)
Summary: Detached from 2/2 processes successfully on nid004155.
Summary: Detached from 2/2 processes successfully on nid004156.
```

---

## 2. ubt-top: Real-Time Cluster TUI & Straggler Dashboard

### Overview & CLI Reference
`ubt-top` is an interactive cluster dashboard designed for online monitoring (Scenario A). It continuously ingests `node_<id>.json` snapshots written by the `ubpf_live_exporter` on each compute node and renders a real-time terminal display.

```text
Usage: ubt-top [options]

Options:
  -j, --job-id <ID>     Slurm Job ID to monitor
  -d, --dir <PATH>      Directory containing live JSON snapshots
  -i, --interval <SEC>  Refresh cadence in seconds (default: 1.0s)
  --json                Emit cluster aggregation JSON once per interval
  --once                Execute single snapshot reduction and exit
  -h, --help            Display this help message
```

---

### Interactive Terminal Dashboard

Launch `ubt-top` pointing to a job ID or snapshot directory:

```bash
./bin/ubt-top -j 58893883
```

**Interactive TUI Interface**:
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
@barrier_max_                       292188         72074          364262       2
@comp_dur_sta                     14448029       3061178        17509207       2
@compute_call                          498           486            1968       4
@iter_max                            10751          4368           30170       4

[NODE TOPOLOGY & SYNC STATUS]
Node ID   Hostname            Epoch     Latency Lag (ms)    Status         
---------------------------------------------------------------------------
0         nid004155           74        0.00                [HEALTHY]
1         nid004156           74        0.87                [HEALTHY]

[Press Ctrl+C to stop monitoring]
```

---

### Automated Straggler Node Detection

`ubt-top` computes the population mean $\mu$ and standard deviation $\sigma$ across all nodes for each metric:
$$\text{Skew Score} = \frac{\mu - x_{\text{node}}}{\sigma}$$

When a node's metric throughput or latency falls below $2\sigma$ from the cluster median, `ubt-top` automatically flags the node with a visual `[STRAGGLER]` warning and details the latency delta.

---

### Single-Shot Mode & Machine-Readable JSON Streaming

- **Single-Shot Reduction (`--once`)**: Useful for automated testing and CI verification:
  ```bash
  ./bin/ubt-top -j 58893883 --once
  ```
- **Machine-Readable JSON Streaming (`--json`)**: Ingest live cluster metrics into Prometheus, Grafana, or Datadog pipelines:
  ```bash
  ./bin/ubt-top -j 58893883 --json --interval 2
  ```

---

## 3. ubt-cat: Trace Container Decoder & Perfetto Exporter

### Overview & CLI Reference

`ubt-cat` processes binary `.ubt` / `.ubpt` trace containers created during application execution. It decodes LZ4-compressed 2MB chunk streams, checks CRC32 block integrity, and merges event records from multiple nodes into a unified chronological timeline.

```text
Usage: ubt-cat [OPTIONS] <file1.ubpt / dir> [file2.ubpt ...]

Options:
  -i, --info, --summary    Display container file metadata and chunk stats
  -d, --dump               Print trace records in chronological order (default)
  -m, --merge              Merge multiple node container streams chronologically
  -o, --output <file.txt>  Direct decoded output to specified file
  -c, --chrome <file.json> Export trace records to Chrome Tracing JSON format
  -j, --job <JOBID>        Target all containers matching Slurm Job ID
  -h, --help               Display this help message
```

---

### Inspecting Container Metadata & Compression (`--info`)

To inspect internal chunk structure, record counts, and LZ4 compression savings:

```bash
./bin/ubt-cat -j 58893883 -i
```

**Real Output**:
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

### Dumping Chronological Event Logs (`--dump`)

Stream decoded, human-readable text logs directly to stdout:

```bash
./bin/ubt-cat --dump traces/ubpftrace_58893883_node_0.ubpt | head -n 10
```

---

### Multi-Node K-Way Min-Heap Merge (`--merge`)

When running across multiple compute nodes, each node produces its own container file (`ubpftrace_<jobid>_node_<nid>.ubpt`).

`ubt-cat` automatically merges all files into a single globally sorted chronological sequence using an $O(N \log K)$ K-way min-heap:

```bash
./bin/ubt-cat -j 58893883 -m -d | head -n 25
```

---

### Exporting to Perfetto / Chrome Tracing (`--chrome`)

Export container event streams into Google Chrome DevTools / Perfetto timeline format:

```bash
./bin/ubt-cat -j 58893883 -m --chrome cluster_timeline.json
```

#### Viewing the Visual Timeline:
1. Open **[ui.perfetto.dev](https://ui.perfetto.dev)** (or `chrome://tracing` in Chromium/Google Chrome).
2. Click **"Open trace file"** and select `cluster_timeline.json`.
3. An interactive multi-rank Gantt chart appears with rank swimlanes, zoomable nanosecond timelines, and function duration blocks.
