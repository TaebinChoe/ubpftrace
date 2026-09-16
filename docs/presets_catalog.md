# Tracing Presets Catalog

`ubpftrace` includes six pre-built tracing presets located in the [`presets/`](file:///pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/presets/) directory. These scripts target common performance bottlenecks in HPC simulations, Distributed AI / LLM Training, Parallel File Systems (Lustre), and Multi-GPU Computing.

For an in-depth empirical evaluation, architectural analysis of bottlenecks, and multi-node execution diagnostics across Perlmutter CPU and GPU clusters, refer to the [Presets Evaluation Guide](file:///pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/docs/presets_evaluation.md).

---

## Catalog Summary Matrix

| Preset Script | Probed Interfaces & Libraries | Core BPF Maps Generated | Target Bottleneck |
| :--- | :--- | :--- | :--- |
| **[`ai_checkpoint_lustre.bt`](file:///pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/presets/ai_checkpoint_lustre.bt)** | `libc.so.6` (`pwrite64`, `write`, `fsync`, `fdatasync`) + `lustre_ost` | `@ost_write_bytes`, `@ost_write_latency_us`, `@ost_write_stats_us`, `@ost_write_ops`, `@fsync_latency_us`, `@fsync_stats_us`, `@fsync_count` | Multi-OST Striped I/O & Storage Commit Latencies |
| **[`mpi_p2p_traffic.bt`](file:///pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/presets/mpi_p2p_traffic.bt)** | `libmpi.so` (`MPI_Send`, `MPI_Isend`, `MPI_Recv`, `MPI_Irecv`, `MPI_Sendrecv`) | `@traffic_matrix_bytes[src, dst]`, `@rx_matrix_bytes[dst, src]`, `@tx_bytes[rank]`, `@rx_bytes[rank]`, `@msg_size_bytes[op]` | Pairwise Rank Traffic Matrices & Fan-in Congestion |
| **[`mpi_straggler_detector.bt`](file:///pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/presets/mpi_straggler_detector.bt)** | `libmpi.so` (`MPI_Barrier`, `MPI_Allreduce`, `MPI_Alltoall`, `MPI_Waitall`) | `@barrier_wait_us[rank]`, `@barrier_stats_us[rank]`, `@allreduce_latency_us[rank]`, `@allreduce_stats_us[rank]`, `@waitall_stats_us[rank]` | Late-Arriving Ranks & Collective Tail Latency |
| **[`openmp_hybrid_contention.bt`](file:///pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/presets/openmp_hybrid_contention.bt)** | `libgomp.so.1` (`GOMP_barrier`, `GOMP_critical_start`, `GOMP_parallel`) | `@omp_barrier_wait_us[tid]`, `@omp_barrier_stats_us[tid]`, `@critical_lock_wait_us[tid]`, `@critical_lock_stats_us[tid]`, `@total_barrier_stall_us` | Thread Workload Imbalance & Critical Lock Convoys |
| **[`cuda_sync_bubbles.bt`](file:///pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/presets/cuda_sync_bubbles.bt)** | `libcudart.so` (`cudaStreamSynchronize`, `cudaDeviceSynchronize`, `cudaEventSynchronize`, `cudaMemcpy`) | `@sync_stall_us[api]`, `@sync_stats_us[api]`, `@rank_sync_stall_us[rank]`, `@sync_memcpy_bytes`, `@sync_calls[api]` | Blocking Host-GPU Sync & Pipeline Bubbles |
| **[`nccl_collective_skew.bt`](file:///pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/presets/nccl_collective_skew.bt)** | `libnccl.so` (`ncclAllReduce`, `ncclReduceScatter`, `ncclAllGather`, `ncclBroadcast`) | `@nccl_latency_us[op]`, `@nccl_stats_us[op]`, `@rank_allreduce_stats[rank]`, `@rank_allreduce_us[rank]`, `@total_elements[op]` | Distributed Ring Collective Skew & Tail Latencies |

---

## 1. `ai_checkpoint_lustre.bt`: AI Checkpoint & Lustre OST Profiler

### Target Use Case
PyTorch / Megatron-LM model checkpointing and parallel I/O. Intercepts POSIX I/O calls, dynamically extracts the Lustre Object Storage Target (OST) index using the `lustre_ost(fd, offset)` helper, and profiles per-OST bandwidth, latency distributions, and commit delays.

### Intercepted Probes
- `uprobe:libc:pwrite64`, `uretprobe:libc:pwrite64`
- `uprobe:libc:write`, `uretprobe:libc:write`
- `uprobe:libc:fdatasync`, `uretprobe:libc:fdatasync`, `uprobe:libc:fsync`, `uretprobe:libc:fsync`

### Launch Command
```bash
# Standalone execution on striped checkpoint benchmark
./bin/ubpftrace -c "./examples/apps/lustre_io_app" presets/ai_checkpoint_lustre.bt
```

### Execution Output
```text
Attached 9 probes

@fsync_count: 2
@fsync_latency_us:
[512, 1K)              1 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
[1K, 2K)               1 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|

@fsync_stats_us: { .count = 2, .average = 1088, .total = 2177 }

@ost_write_bytes[159]: 8388608
@ost_write_bytes[160]: 8388608
@ost_write_bytes[161]: 8388608
@ost_write_bytes[162]: 8388608

@ost_write_ops[159]: 8
@ost_write_ops[160]: 8
@ost_write_ops[161]: 8
@ost_write_ops[162]: 8

@ost_write_stats_us[159]: { .count = 8, .average = 877,  .total = 7018 }
@ost_write_stats_us[162]: { .count = 8, .average = 882,  .total = 7059 }
@ost_write_stats_us[160]: { .count = 8, .average = 963,  .total = 7705 }
@ost_write_stats_us[161]: { .count = 8, .average = 1001, .total = 8012 }

@ost_write_latency_us[159]:
[512, 1K)              7 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
[1K, 2K)               1 |@@@@@@@                                             |
```

---

## 2. `mpi_p2p_traffic.bt`: Point-to-Point Communication Traffic Matrix

### Target Use Case
Analyzes point-to-point communication volumes across synchronous and asynchronous primitives (`MPI_Send`, `MPI_Isend`, `MPI_Recv`, `MPI_Irecv`, `MPI_Sendrecv`). Derives the pairwise communication traffic matrix (`@traffic_matrix_bytes[src, dst]`), per-rank Tx/Rx totals, and message size histograms.

### Intercepted Probes
- `uprobe:mpi:MPI_Send`, `uprobe:mpi:MPI_Isend`
- `uprobe:mpi:MPI_Recv`, `uprobe:mpi:MPI_Irecv`
- `uprobe:mpi:MPI_Sendrecv`

### Launch Command
```bash
srun -N 2 -n 4 bash -c "MPICH_GPU_SUPPORT_ENABLED=0 MPICH_COLL_OPT_OFF=1 ./bin/ubpftrace -c ./examples/apps/mpi_p2p_app presets/mpi_p2p_traffic.bt"
```

### Multi-Node Execution Output (4 Ranks on 2 Nodes)
```text
=== Rank 0 (Root Aggregator on nid004144) ===
@rx_bytes[0]: 2420736 (~2.42 MB received)
@tx_bytes[0]: 62016

@rx_matrix_bytes[0, 1]: 798720  (From Rank 1)
@rx_matrix_bytes[0, 2]: 786432  (From Rank 2)
@rx_matrix_bytes[0, 3]: 835584  (From Rank 3)

@traffic_matrix_bytes[0, 1]: 49344
@traffic_matrix_bytes[0, 2]: 192
@traffic_matrix_bytes[0, 3]: 12480

@msg_size_bytes[MPI_Recv]:
[256K, 512K)           9 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|

=== Worker Ranks (Ranks 1, 2, 3 on nid004144/nid004145) ===
@tx_bytes[1]: 847872 | @rx_bytes[1]: 61632
@tx_bytes[2]: 847872 | @rx_bytes[2]: 61632
@tx_bytes[3]: 847872 | @rx_bytes[3]: 61632

@traffic_matrix_bytes[1, 0]: 798720  (Worker 1 -> Root Rendezvous)
@traffic_matrix_bytes[2, 0]: 786432  (Worker 2 -> Root Rendezvous)
@traffic_matrix_bytes[3, 0]: 835584  (Worker 3 -> Root Rendezvous)
```

---

## 3. `mpi_straggler_detector.bt`: MPI Collective Straggler & Barrier Imbalance

### Target Use Case
Identifies ranks that arrive late to global collectives (`MPI_Barrier`, `MPI_Allreduce`, `MPI_Alltoall`, `MPI_Waitall`), causing peer ranks to stall idle. Generates per-rank execution counts, latency log-histograms, and summary statistics.

### Intercepted Probes
- `uprobe:mpi:MPI_Barrier`, `uretprobe:mpi:MPI_Barrier`
- `uprobe:mpi:MPI_Allreduce`, `uretprobe:mpi:MPI_Allreduce`
- `uprobe:mpi:MPI_Alltoall`, `uretprobe:mpi:MPI_Alltoall`
- `uprobe:mpi:MPI_Waitall`, `uretprobe:mpi:MPI_Waitall`

### Launch Command
```bash
srun -N 2 -n 4 bash -c "MPICH_GPU_SUPPORT_ENABLED=0 MPICH_COLL_OPT_OFF=1 ./bin/ubpftrace -c ./examples/apps/hpc_app presets/mpi_straggler_detector.bt"
```

### Multi-Node Execution Output (4 Ranks on 2 Nodes)
```text
=== Rank 0 (Computational Straggler: 25ms compute) ===
@barrier_calls[0]: 5
@barrier_stats_us[0]: { .count = 5, .average = 40, .total = 201 }
@barrier_wait_us[0]:
[8, 16)                2 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
[128, 256)             1 |@@@@@@@@@@@@@@@@@@@@@@@@@@                          |

=== Rank 1 (Fast Worker Rank: 1.5ms compute) ===
@barrier_calls[1]: 5
@barrier_stats_us[1]: { .count = 5, .average = 18843, .total = 94218 }
@barrier_wait_us[1]:
[8, 16)                1 |@@@@@@@@@@@@@                                       |
[16K, 32K)             4 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|

=== Rank 2 (Fast Worker Rank: 1.5ms compute) ===
@barrier_stats_us[2]: { .count = 5, .average = 18857, .total = 94289 }

=== Rank 3 (Fast Worker Rank: 1.5ms compute) ===
@barrier_stats_us[3]: { .count = 5, .average = 18854, .total = 94270 }
```

---

## 4. `openmp_hybrid_contention.bt`: OpenMP Hybrid Barrier & Lock Contention

### Target Use Case
Profiles multi-threaded CPU OpenMP execution in hybrid MPI+OpenMP codes. Identifies intra-node thread load imbalances during barrier synchronization (`GOMP_barrier`), critical section lock stalls (`GOMP_critical_start`), and overall parallel region execution durations (`GOMP_parallel`).

### Intercepted Probes
- `uprobe:gomp:GOMP_barrier`, `uretprobe:gomp:GOMP_barrier`
- `uprobe:gomp:GOMP_critical_start`, `uretprobe:gomp:GOMP_critical_start`
- `uprobe:gomp:GOMP_parallel`, `uretprobe:gomp:GOMP_parallel`

### Launch Command
```bash
OMP_NUM_THREADS=16 ./bin/ubpftrace -c ./examples/apps/omp_app presets/openmp_hybrid_contention.bt
```

### Execution Output (16 Active Threads)
```text
@total_barrier_stall_us: 1594952 (1.59 seconds cumulative barrier stall)

@omp_barrier_stats_us[95288]: { .count = 8, .average = 17456, .total = 139649 }
@omp_barrier_stats_us[95297]: { .count = 8, .average = 17213, .total = 137709 }
@omp_barrier_stats_us[95289]: { .count = 8, .average = 16605, .total = 132840 }
@omp_barrier_stats_us[95295]: { .count = 8, .average = 14658, .total = 117264 }
@omp_barrier_stats_us[95291]: { .count = 8, .average = 14101, .total = 112814 }

@critical_lock_wait_us[95302]:
[8K, 16K)              1 |@@@@@@@@@@@@@@@@@                                   |
[16K, 32K)             3 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|

@parallel_region_stats_us[95276]: { .count = 1, .average = 162890, .total = 162890 }
```

---

## 5. `cuda_sync_bubbles.bt`: CUDA Host-Device Synchronization Bubbles

### Target Use Case
Detects host CPU stalls caused by synchronous CUDA Runtime operations (`cudaStreamSynchronize`, `cudaDeviceSynchronize`, `cudaEventSynchronize`, `cudaMemcpy`). Pinpoints synchronous serialization bubbles that prevent overlapping compute kernels with data movement.

### Intercepted Probes
- `uprobe:cudart:cudaStreamSynchronize`, `uretprobe:cudart:cudaStreamSynchronize`
- `uprobe:cudart:cudaDeviceSynchronize`, `uretprobe:cudart:cudaDeviceSynchronize`
- `uprobe:cudart:cudaEventSynchronize`, `uretprobe:cudart:cudaEventSynchronize`
- `uprobe:cudart:cudaMemcpy`, `uretprobe:cudart:cudaMemcpy`

### Launch Command
```bash
./bin/ubpftrace -c ./examples/apps/cuda_sync_app presets/cuda_sync_bubbles.bt
```

### Execution Output
```text
@sync_calls[cudaMemcpy_Sync]: 2
@sync_calls[cudaStreamSynchronize]: 1
@sync_calls[cudaDeviceSynchronize]: 1
@sync_memcpy_bytes: 4195328 (4.19 MB)

@sync_stats_us[cudaMemcpy_Sync]:       { .count = 2, .average = 603950, .total = 1207900 }
@sync_stats_us[cudaStreamSynchronize]: { .count = 1, .average = 22,     .total = 22 }
@sync_stats_us[cudaDeviceSynchronize]: { .count = 1, .average = 14,     .total = 14 }

@rank_sync_stall_us[0]: 1207936 (1.208 seconds total stall)

@sync_stall_us[cudaMemcpy_Sync]:
[16, 32)               1 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
[1M, 2M)               1 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
```

---

## 6. `nccl_collective_skew.bt`: Multi-GPU NCCL Collective Skew & Ring Latency

### Target Use Case
Monitors Distributed Deep Learning frameworks (Megatron-LM, DeepSpeed, PyTorch DDP/FSDP). Profiles collective synchronization latencies (`ncclAllReduce`, `ncclReduceScatter`, `ncclAllGather`, `ncclBroadcast`) across GPU ranks, tracking tensor element volumes and isolating straggler GPU ranks.

### Intercepted Probes
- `uprobe:nccl:ncclAllReduce`, `uretprobe:nccl:ncclAllReduce`
- `uprobe:nccl:ncclReduceScatter`, `uretprobe:nccl:ncclReduceScatter`
- `uprobe:nccl:ncclAllGather`, `uretprobe:nccl:ncclAllGather`
- `uprobe:nccl:ncclBroadcast`, `uretprobe:nccl:ncclBroadcast`

### Launch Command
```bash
srun -N 2 -n 8 env LD_LIBRARY_PATH=/pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/examples/apps ./bin/ubpftrace -c ./examples/apps/nccl_collective_app presets/nccl_collective_skew.bt
```

### Multi-Node Execution Output (8 GPUs Across 2 Nodes)
```text
=== Rank 0 (Straggler GPU on nid001065) ===
@rank_allreduce_stats[0]: { .count = 4, .average = 20064, .total = 80258 }
@rank_allreduce_us[0]:
[16K, 32K)             4 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|

=== Healthy Peer GPUs (Ranks 1..7 across nid001065 and nid001308) ===
@rank_allreduce_stats[1]: { .count = 4, .average = 2563, .total = 10252 }
@rank_allreduce_stats[4]: { .count = 4, .average = 2564, .total = 10258 }
@rank_allreduce_stats[5]: { .count = 4, .average = 2563, .total = 10252 }
@rank_allreduce_stats[6]: { .count = 4, .average = 2564, .total = 10258 }
@rank_allreduce_stats[7]: { .count = 4, .average = 2563, .total = 10255 }

@total_elements[AllReduce]:     4,194,304 floats (16.78 MB)
@total_elements[Broadcast]:     2,097,152 floats (8.39 MB)
@total_elements[ReduceScatter]: 1,048,576 floats (4.19 MB)
@total_elements[AllGather]:     1,048,576 floats (4.19 MB)
```
