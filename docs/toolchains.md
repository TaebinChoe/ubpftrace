# Companion Toolchains Manual: ubpftrace-cat & ubpftrace-top

`ubpftrace` includes two standalone companion utilities engineered for offline post-mortem analysis and real-time operations:
1. **`ubpftrace-cat`**: Binary `.ubpf` trace container decoder, multi-node timeline merger, and Google Chrome / Perfetto visualizer exporter.
2. **`ubpftrace-top`**: Interactive real-time cluster TUI dashboard and automated straggler node detector.

---

## Table of Contents
1. [ubpftrace-cat: Container Decoder & Perfetto Exporter](#1-ubpftrace-cat-container-decoder--perfetto-exporter)
   - [Overview & CLI Reference](#overview--cli-reference-1)
   - [Inspecting Container Metadata (--info)](#inspecting-container-metadata---info)
   - [Dumping Chronological Event Logs (--dump)](#dumping-chronological-event-logs---dump)
   - [Multi-Node K-Way Min-Heap Merge (--merge)](#multi-node-k-way-min-heap-merge---merge)
   - [Exporting to Perfetto / Chrome Tracing (--chrome)](#exporting-to-perfetto--chrome-tracing---chrome)
2. [ubpftrace-top: Real-Time Cluster TUI & Straggler Dashboard](#2-ubpftrace-top-real-time-cluster-tui--straggler-dashboard)
   - [Overview & CLI Reference](#overview--cli-reference-2)
   - [Interactive Terminal Dashboard](#interactive-terminal-dashboard)
   - [Automated Straggler Node Detection](#automated-straggler-node-detection)
   - [Machine-Readable JSON Streaming (--json)](#machine-readable-json-streaming---json)

---

## 1. ubpftrace-cat: Container Decoder & Perfetto Exporter

### Overview & CLI Reference
`ubpftrace-cat` processes binary `.ubpf` trace containers created during application execution. It decodes LZ4-compressed 2MB chunk streams, checks CRC32 block integrity, and merges event records from multiple nodes into a unified chronological timeline.

```text
Usage: ubpftrace-cat [OPTIONS] <file1.ubpf / dir> [file2.ubpf ...]

Options:
  -i, --info, --summary    Display container file metadata and chunk stats
  -d, --dump               Print trace records in chronological order (default)
  -m, --merge              Merge multiple node container streams chronologically
  -o, --output <file.txt>  Direct decoded output to specified file
  -c, --chrome <file.json> Export trace records to Chrome Tracing JSON format
  -h, --help               Display this help message
```

---

### Inspecting Container Metadata (`--info`)
To inspect the internal chunk structure, record counts, and LZ4 compression efficiency of a trace file:

```bash
./bin/ubpftrace-cat --info ./traces/trace_node_0.ubpf
```

**Sample Output**:
```text
================================================================================
Container File: ./traces/trace_node_0.ubpf
================================================================================
File Size:              4,194,560 bytes (4.00 MB)
Magic:                  0x55425046 ("UBPF")
Version:                1.0
Job ID:                 924727
Node ID:                3532691273
Hostname:               nid001234
Codec:                  LZ4 (0x01)
Monotonic Base:         954874605933206 ns
Realtime Base (Epoch):  1789432190000000000 ns
Total Chunks:           2
Total Records:          248,512
Total Dropped Events:   0
Uncompressed Size:      16,777,216 bytes (16.00 MB)
Compressed Size:        4,194,304 bytes (4.00 MB)
Compression Ratio:      4.00x
CRC32 Status:           ALL CHUNKS VALID
================================================================================
```

---

### Dumping Chronological Event Logs (`--dump`)
To stream decoded, human-readable text logs directly to stdout or a file:

```bash
./bin/ubpftrace-cat --dump ./traces/trace_node_0.ubpf -o decoded_events.txt
```

**Sample Output**:
```text
[TS 954874605940120 ns] [Rank  0] [TID 1042] [Event 1] MPI_Send(dest=1, bytes=65536, tag=10)
[TS 954874605942850 ns] [Rank  1] [TID 1043] [Event 1] MPI_Recv(src=0, bytes=65536, tag=10)
[TS 954874605945100 ns] [Rank  0] [TID 1042] [Event 2] Lustre_Write(fd=4, ost=12, bytes=2097152)
```

---

### Multi-Node K-Way Min-Heap Merge (`--merge`)
When running across multiple compute nodes, each node produces its own container file (`trace_node_0.ubpf`, `trace_node_1.ubpf`, ...).

`ubpftrace-cat` automatically merges all files into a single globally sorted chronological sequence using an $O(N \log K)$ K-way min-heap:

```bash
# Pass all files explicitly or provide the entire directory
./bin/ubpftrace-cat --merge ./traces/ -o cluster_merged_trace.txt
```

---

### Exporting to Perfetto / Chrome Tracing (`--chrome`)
To visually inspect execution timelines, MPI communication dependencies, and GPU kernel latencies:

```bash
./bin/ubpftrace-cat --merge ./traces/*.ubpf --chrome cluster_timeline.json
```

#### Viewing the Visual Timeline:
1. Open **[ui.perfetto.dev](https://ui.perfetto.dev)** (or `chrome://tracing` in Chromium/Google Chrome).
2. Click **"Open trace file"** and select `cluster_timeline.json`.
3. An interactive multi-rank Gantt chart appears with rank swimlanes, zoomable nanosecond timelines, and function duration blocks.

---

## 2. ubpftrace-top: Real-Time Cluster TUI & Straggler Dashboard

### Overview & CLI Reference
`ubpftrace-top` is an interactive cluster dashboard designed for live operations (Scenario A). It continuously ingests `node_<id>.json` snapshots written by the `ubpf_live_exporter` on each physical node and renders a real-time terminal display.

```text
Usage: ubpftrace-top [options]

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
Launch `ubpftrace-top` pointing to the snapshot directory:

```bash
./bin/ubpftrace-top --dir /pscratch/sd/s/sgkim/tchoe_home/my_traces/.live_snapshots
```

**Interactive TUI Interface**:
```text
========================================================================================
 ubpftrace-top  --  Cluster Telemetry & Straggler Dashboard   [Refresh: 1.0s]
 Job ID: 924727 | Nodes Active: 4/4 | Total Events: 1,420,950 | Epoch: 18
========================================================================================
 Node ID      Hostname    Active Ranks   Throughput (ev/s)   Metric (@sum)   Status
----------------------------------------------------------------------------------------
 3532691273   nid001234   4 ranks        120,400 ev/s        4,820,000       HEALTHY
 3532691274   nid001235   4 ranks        119,850 ev/s        4,795,000       HEALTHY
 3532691275   nid001236   4 ranks        121,100 ev/s        4,845,000       HEALTHY
 3532691276   nid001237   4 ranks         18,200 ev/s          728,000       STRAGGLER (!)
----------------------------------------------------------------------------------------
 Cluster Statistics:
   Mean Throughput: 94,887 ev/s | StdDev: 44,280 | Min: 18,200 | Max: 121,100
   ALERT: Node nid001237 is 5.2x slower than cluster median (GPU Barrier Skew detected)
========================================================================================
 [Q] Quit | [R] Force Refresh | [S] Sort by Throughput | [M] Sort by Metric
```

---

### Automated Straggler Node Detection
`ubpftrace-top` computes the population mean $\mu$ and standard deviation $\sigma$ across all nodes for each metric:
$$\text{Skew Score} = \frac{\mu - x_{\text{node}}}{\sigma}$$

When a node's event rate or metric throughput falls below $2\sigma$ from the cluster median, `ubpftrace-top` automatically:
1. Flags the node with a visual `STRAGGLER (!)` warning.
2. Identifies the specific bottleneck (e.g. MPI collective imbalance, slow Lustre OST, GPU sync bubble).

---

### Machine-Readable JSON Streaming (`--json`)
To integrate cluster profiling directly into external monitoring stacks (e.g. Prometheus, Grafana, Datadog):

```bash
./bin/ubpftrace-top --dir ./live_snapshots --json
```

**JSON Output (1 line per interval)**:
```json
{"timestamp_ns":954874605933206,"job_id":924727,"nodes_active":4,"total_throughput_eps":379550,"stragglers":["nid001237"],"metrics":{"total_calls":15188000,"mean_latency_us":14.2}}
```
