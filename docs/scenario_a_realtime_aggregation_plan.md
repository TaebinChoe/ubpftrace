# Comprehensive Real-Time Architecture Design (Scenario A & Live Event Streaming)

## Executive Summary

While **Scenario B** (post-run time-bucketed aggregation via isolated `MPI_Comm_dup` reduction) remains `ubpftrace`'s primary paradigm for maximum application throughput and strict zero-jitter benchmarking, practical production cluster operations (e.g., multi-day LLM pre-training runs, interactive debugging, and large-scale HPC simulations) demand two real-time observability capabilities:

1. **Periodic Metric Snapshotting (Refined Scenario A)**: Near-real-time periodic metric aggregation and inspection (from sub-second cadences down to 100ms up to multi-minute intervals) without inter-node MPI collective synchronization stalls or PFS saturation.
2. **Low-Latency Micro-Buffered Event Streaming (Live stdout / printf)**: High-throughput, sub-frame latency terminal trace streaming (`printf(...)`, `read()`/`write()` hooks) using dual-trigger micro-buffering (soft timer + size threshold) to mitigate syscall thrashing while guaranteeing intra-node chronological monotonicity.

This document establishes the unified engineering architecture for both real-time paths, strictly maintaining `ubpftrace`'s fundamental invariants: unprivileged user-space execution, zero compute-thread preemption, bounded OOM safety, and single-writer-per-node Lustre scalability.

---

## 1. Unified Real-Time System Architecture

```
+-------------------------------------------------------------------------------------------------------------+
| Compute Node K (e.g., Perlmutter nid004207)                                                                 |
|                                                                                                             |
|  +--------------------+  +--------------------+                                                             |
|  | MPI Rank 0 (Local) |  | MPI Rank 1..P-1    |  (Pure in-memory atomic updates & lockless ring reservations)   |
|  +---------+----------+  +---------+----------+                                                             |
|            |                       |                                                                        |
|            |  +--------------------+                                                                        |
|            v  v                                                                                             |
|    +---------------------------------------------------------------------------------------------------+    |
|    | Node-Local POSIX Shared Memory (/dev/shm/bpftime_shm_job_<id>_node_<nid>)                         |    |
|    |  ├── Active In-Memory BPF Maps (Scalar values, Histograms, Associative Per-Rank Arrays)           |    |
|    |  └── Epoch-Hazard Double-Buffered Event Ring Buffers (Local Ranks 0..P-1 Event Streams)           |    |
|    +---------------------------------+-----------------------------------------------------------------+    |
|                                      |                                                                      |
|                                      v (Lock-free asynchronous traversal & micro-batch drain)               |
|    +---------------------------------------------------------------------------------------------------+    |
|    | ubpf_async_io_worker (Local Rank 0 Leader Background Thread)                                      |    |
|    | - Priority: SCHED_IDLE / nice +19 on unallocated CPU cores (Zero compute thread preemption)       |    |
|    |                                                                                                   |    |
|    |  [PATH 1: Scenario A Metric Exporter]               [PATH 2: Micro-Buffered Live Streamer]        |    |
|    |  - Periodic soft timer (--live / --live-ms)         - Dual-trigger: 20ms timer OR 8KB buffer       |    |
|    |  - Traverses active BPF maps                        - Chronologically merges local rank events     |    |
|    |  - Serializes to compact JSON snapshot              - Assembles batch lines & flushes to stdout    |    |
|    |  - Atomic write: .tmp.<pid> -> rename()             - Reduces write() syscall overhead by 1000x    |    |
|    +-----------------+---------------------------------------------------+-----------------------------+    |
+----------------------|---------------------------------------------------|----------------------------------+
                       |                                                   |
                       v                                                   v
+-------------------------------------------------------+  +--------------------------------------------------+
| Storage / Scratch ($SCRATCH/.ubpftrace_live_<jobid>/) |  | Standard Output (Terminal / Slurm Log Stream)    |
|   ├── node_0.json (~1.5 KB, updated atomically)       |  |   [Node 0, Rank 2, 12.001s] read fd=3 len=4096   |
|   ├── node_1.json (~1.5 KB, updated atomically)       |  |   [Node 0, Rank 0, 12.002s] write fd=3 len=4096  |
|   └── node_K.json (~1.5 KB, updated atomically)       |  |   [Node 1, Rank 5, 12.003s] read fd=7 len=8192   |
+--------------------------+----------------------------+  |   (Intra-node ordered, Inter-node arrival order) |
                           |                               +--------------------------------------------------+
                           v (Non-blocking poll & merge)
+-------------------------------------------------------------------------------------------------------------+
| Head / Login Node: `ubpftrace-top` (Standalone Cluster Dashboard CLI / TUI)                                 |
|   - Multi-Tier Reduction: Rank -> Node -> Global Cluster (Global Max/Min, Histograms, Tail Skew)            |
|   - Real-Time TUI Dashboard / JSON Stream for Prometheus/Grafana pipelines                                   |
+-------------------------------------------------------------------------------------------------------------+
```

---

## 2. Periodic Metric Snapshotting (Refined Scenario A)

### 2.1 Dynamic & Sub-Second Interval Configuration
Scenario A supports arbitrary, user-configurable snapshot frequencies without hardcoded duration limits:
* **Standard Cadences**: `--live <seconds>` (e.g., `--live 1` for 1 Hz updates, `--live 30` for 30s intervals).
* **Sub-Second Rapid Inspection**: `--live-ms <milliseconds>` (e.g., `--live-ms 100` for 100ms high-resolution transient detection).
* **Environment Overrides**: `UBPFTRACE_LIVE_INTERVAL_MS` (takes precedence if defined).

### 2.2 Non-Intrusive Export & Lock-Free Map Traversal
To eliminate lock contention and cacheline invalidation on active compute ranks:
1. **Relaxed Atomic Traversal**:
   - 64-bit scalar metrics (`@sum`, `@count`, `@max`, `@min`) are read via relaxed atomic loads directly from `/dev/shm`.
   - Associative arrays (`@metric[rank]`) are scanned linearly across allocated buckets without taking exclusive reader-writer locks.
2. **Worker Scheduling & CPU Isolation**:
   - Background export runs on the `ubpf_async_io_worker` thread in `local_rank == 0`.
   - Thread priority is set via `pthread_setschedparam(..., SCHED_IDLE)` with fallback to `setpriority(PRIO_PROCESS, 0, 19)`.
   - CPU affinity is detached from the compute rank's core binding mask, executing exclusively on unassigned cores or hyperthreads.

### 2.3 Atomic POSIX File Replacement Semantics
To prevent monitoring tools (`ubpftrace-top`) from observing partial writes or torn JSON structures on shared filesystems (Lustre / GPFS / NFS):
1. The exporter serializes the active metrics into a hidden staging file:
   `$SCRATCH/.ubpftrace_live_<job_id>/.node_<nid>.json.tmp.<pid>`
2. The exporter explicitly flushes and commits user-space buffers via `std::fflush()` and `fsync()`.
3. The exporter executes an atomic POSIX filesystem rename:
   ```cpp
   ::rename(temp_filepath.c_str(), final_filepath.c_str());
   ```
   Under POSIX and Lustre distributed lock manager (LDLM) semantics, `rename()` within the same directory is strictly atomic. Readers opening `node_<nid>.json` will observe either the previous consistent snapshot or the new snapshot, with zero corrupt intermediate states.

### 2.4 Multi-Tier Granularity Parity
Intermediate snapshots provide identical structural granularity as post-run summaries:
* **Temporal Epoch**: Nanosecond wall-clock timestamp and monotonically increasing export epoch.
* **Process / Rank Level**: Per-rank metric distributions (`@metric[rank]`) for intra-node load balancing analysis.
* **Node Level**: Node-aggregated totals, maximums, minimums, and local power-of-2 histogram distributions.
* **Metadata Schema**:
  ```json
  {
    "version": 1,
    "job_id": 123456,
    "node_id": 0,
    "nodename": "nid004207",
    "local_ranks": [0, 1, 2, 3],
    "timestamp_ns": 1726284900123456789,
    "epoch": 12,
    "maps": {
      "max_lat_ns": {
        "type": "max",
        "node_max": 1284500,
        "per_rank": { "0": 112000, "1": 1284500, "2": 98000, "3": 104000 }
      },
      "io_size_bytes": {
        "type": "hist",
        "buckets": { "4096": 1250, "65536": 480, "1048576": 12 }
      }
    }
  }
  ```

---

## 3. Low-Latency Micro-Buffered Event Streaming (Live stdout / printf)

### 3.1 Motivation & Mechanics
When users trace dynamic events with immediate terminal display requirements (e.g., `printf("Rank %d: read %d bytes\n", rank, len)`), existing systems suffer from a binary dilemma:
* *Pure Unbuffered I/O (`setvbuf(stdout, NULL, _IONBF, 0)`)*: Triggers a `write()` syscall per probe event ($\approx 1\text{ to }5\ \mu\text{s}$ overhead per event), causing catastrophic application stalls and terminal TTY backpressure.
* *Bulk Container Buffering (2MB / 4MB blocks)*: Causes events to sit in memory for minutes under moderate event rates, destroying interactive debugging utility.

### 3.2 Dual-Trigger Micro-Buffering Engine
`ubpftrace` introduces a dedicated **Micro-Buffered Streaming Path** inside `ubpf_async_io_worker`:

```
               +-------------------------------------------+
               | Incoming Dynamic Probe Events from SHM    |
               +---------------------+---------------------+
                                     |
                                     v
                       +---------------------------+
                       | Local Ring Buffer Drain   |
                       | (Intra-Node Sorted Queue) |
                       +-------------+-------------+
                                     |
                                     v
                       +---------------------------+
                       | Micro-Batch Line Buffer   | <---+ (Accumulates formatted text)
                       +-------------+-------------+     |
                                     |                   |
            +------------------------+------------------------+
            |                                                 |
            v                                                 v
  [Condition 1: Size Threshold]                     [Condition 2: Soft Timer]
  Accumulated Bytes >= --stream-buffer-kb           Elapsed Time >= --stream-flush-ms
  (Default: 8 KB)                                   (Default: 20 ms)
            |                                                 |
            +------------------------+------------------------+
                                     |
                                     v (Single writev/write syscall)
                       +---------------------------+
                       | stdout / Terminal Stream  |
                       +---------------------------+
```

### 3.3 Configurable Streaming Parameters & CLI Flags

| CLI Flag | Environment Variable | Default | Description |
| :--- | :--- | :--- | :--- |
| `--stream` | `UBPFTRACE_STREAM` | `false` | Enable micro-buffered live terminal event streaming. |
| `--stream-flush-ms <ms>` | `UBPFTRACE_STREAM_FLUSH_MS` | `20` | Maximum buffering delay in milliseconds before flushing pending lines. |
| `--stream-buffer-kb <kb>` | `UBPFTRACE_STREAM_BUFFER_KB` | `8` | Buffer capacity in kilobytes triggering immediate batch flush. |

### 3.4 Syscall Mitigation & Performance Invariant
* **Syscall Compression**: Under an event rate of $50{,}000\text{ events/sec}$, unbuffered streaming invokes $50{,}000\text{ write() syscalls/sec}$. With `--stream-flush-ms 20` and `--stream-buffer-kb 8`, syscall frequency drops to $\le 50\text{ syscalls/sec}$ (**$1{,}000\times$ reduction in system call overhead**).
* **Perceived Real-Time Latency**: The maximum human-observable delay is strictly bounded by $\min(20\text{ ms}, \text{buffer\_fill\_time})$, delivering fluid, 50 Hz sub-frame terminal updates.

---

## 4. Defined Ordering Semantics for Live Streaming

| Dimension | Ordering Guarantee | Implementation Mechanism | Rationale |
| :--- | :--- | :--- | :--- |
| **Intra-Node (Local Ranks on Same Node)** | **Strictly Monotonic** | Multi-way merge sort across local rank ring buffers based on 64-bit nanosecond monotonic timestamps (`hdr->timestamp_ns`). | Preserves causality of multi-threaded and multi-rank interactions on the same physical host without cross-node network overhead. |
| **Inter-Node (Across Compute Nodes)** | **Arrival-Order (Unsorted)** | Flushed independently by each node leader directly to stdout / Slurm output stream. | **Strict Zero Inter-Node Barrier Invariant**: Enforcing cluster-wide live timestamp sorting during runtime would require distributed barrier synchronization, destroying HPC compute throughput. |
| **Post-Run Global Merge** | **Strictly Monotonic (Cluster-Wide)** | Handled post-execution by `ubpftrace-cat --merge file_node_*.ubpf`. | $K$-way min-heap container merge across all node files offline. |

---

## 5. Standalone Cluster Aggregator & Live Dashboard: `ubpftrace-top`

A dedicated C++ monitoring utility, `ubpftrace-top`, is added to [`tools/`](file:///pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/tools/) to monitor running jobs from head/login nodes.

### 5.1 Architecture & Multi-Tier Reduction Engine
```cpp
struct node_snapshot {
    uint32_t node_id;
    std::string nodename;
    uint64_t timestamp_ns;
    uint32_t epoch;
    std::vector<uint32_t> local_ranks;
    std::unordered_map<std::string, map_entry> maps;
};
```

1. **Non-Blocking Ingestion**: Scans `$SCRATCH/.ubpftrace_live_<jobid>/node_*.json` using directory polling at the user-specified UI refresh rate.
2. **Asynchronous Skew & Straggler Tracking**:
   - Compares each node's `timestamp_ns` against the cluster median.
   - If $\Delta t_{\text{node}} > 2.5 \times \text{interval}$, the node is flagged with `[LAG / STRAGGLER]`.
3. **Hierarchical Metric Aggregation**:
   - **Rank Leaderboard**: Identifies top-$N$ global outlier ranks across all compute nodes (e.g., ranks suffering highest CUDA sync latency).
   - **Node Statistics**: Calculates mean ($\mu$), standard deviation ($\sigma$), and coefficient of variation ($CV = \sigma / \mu$) across nodes.
   - **Global Cluster Reductions**:
     $$\text{GlobalMax} = \max_{k=1}^N(\text{NodeMax}_k), \quad \text{GlobalMin} = \min_{k=1}^N(\text{NodeMin}_k), \quad \text{GlobalSum} = \sum_{k=1}^N \text{NodeSum}_k$$
     $$\text{MergedHist}[\text{bin}] = \sum_{k=1}^N \text{NodeHist}_k[\text{bin}]$$

### 5.2 Terminal UI & Integration Modes
* **Interactive TUI Mode (Default)**: Fullscreen ANSI/curses dashboard showing cluster health summary, tail-latency percentiles (p50, p95, p99, max), ASCII histogram bars, and top straggler nodes.
* **JSON Stream Mode (`--json`)**: Emits global cluster summary objects to stdout once per interval for direct ingestion into Prometheus, Grafana, or operational telemetry pipelines.

---

## 6. Comprehensive File Manifest

| Component | File Path | Action | Description |
| :--- | :--- | :---: | :--- |
| **Architecture Specification** | [`docs/scenario_a_realtime_aggregation_plan.md`](file:///pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/docs/scenario_a_realtime_aggregation_plan.md) | **Updated** | Unified engineering design for Scenario A and micro-buffered streaming. |
| **Live Exporter Header** | `bpftime/runtime/include/hpc/ubpf_live_exporter.hpp` | **NEW** | Interface for lock-free BPF map serialization and atomic file emission. |
| **Live Exporter Implementation** | `bpftime/runtime/src/hpc/ubpf_live_exporter.cpp` | **NEW** | Lock-free SHM map traversal and JSON serializer implementation. |
| **Micro-Buffered Streamer Header** | `bpftime/runtime/include/hpc/ubpf_micro_streamer.hpp` | **NEW** | Dual-trigger (timer + size) micro-buffering line assembler. |
| **Micro-Buffered Streamer Source** | `bpftime/runtime/src/hpc/ubpf_micro_streamer.cpp` | **NEW** | Intra-node monotonic merge sort and batch stdout flush engine. |
| **Async Worker Extension** | [`bpftime/runtime/include/hpc/ubpf_async_io_worker.hpp`](file:///pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/bpftime/runtime/include/hpc/ubpf_async_io_worker.hpp) / [`ubpf_async_io_worker.cpp`](file:///pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/bpftime/runtime/src/hpc/ubpf_async_io_worker.cpp) | **MODIFY** | Integrate periodic metric export and micro-buffered stream dispatch. |
| **Agent Manager Integration** | [`bpftime/runtime/include/hpc/ubpf_agent_manager.hpp`](file:///pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/bpftime/runtime/include/hpc/ubpf_agent_manager.hpp) / [`ubpf_agent_manager.cpp`](file:///pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/bpftime/runtime/src/hpc/ubpf_agent_manager.cpp) | **MODIFY** | Wire `--live`, `--live-ms`, `--stream`, `--stream-flush-ms`, and `--stream-buffer-kb` configs. |
| **CLI & Parser Options** | [`src/config.h`](file:///pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/src/config.h), [`src/config.cpp`](file:///pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/src/config.cpp), [`src/main.cpp`](file:///pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/src/main.cpp) | **MODIFY** | Expose all CLI flags and environment variable bindings. |
| **Dashboard Tool** | `tools/ubpftrace_top.cpp` | **NEW** | Standalone multi-node snapshot aggregator and real-time TUI dashboard. |
| **Tool Build Configuration** | [`tools/CMakeLists.txt`](file:///pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/tools/CMakeLists.txt) | **MODIFY** | Add `ubpftrace-top` executable build target. |
