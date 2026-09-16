# Flagship Production Presets Catalog

`ubpftrace` includes six pre-built, production-validated tracing presets located in the [`presets/`](file:///pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/presets/) directory. These scripts target the most critical performance bottlenecks in Large-Scale HPC, Distributed AI / LLM Training, Parallel File Systems (Lustre), and Multi-GPU Computing.

---

## Catalog Summary Matrix

| Preset Script | Probed Interfaces & Libraries | Core BPF Maps Generated | Target Bottleneck |
| :--- | :--- | :--- | :--- |
| **`ai_checkpoint_lustre.bt`** | `libc.so.6` (`pwrite64`, `pwrite`, `write`, `fsync`, `fdatasync`) + `lustre_ost` | `@ost_write_bytes`, `@ost_write_latency_us`, `@ost_write_ops`, `@stream_write_bytes`, `@stream_write_latency_us`, `@fsync_latency_us`, `@fsync_stats_us`, `@fsync_count` | Parallel Storage Contention & Overloaded Lustre OSTs |
| **`mpi_straggler_detector.bt`** | `libmpi.so` (`MPI_Barrier`, `MPI_Allreduce`, `MPI_Alltoall`, `MPI_Waitall`) | `@allreduce_calls`, `@allreduce_latency_us`, `@allreduce_stats_us`, `@barrier_calls`, `@barrier_stats_us`, `@barrier_wait_us`, `@total_allreduce_time_us`, `@total_barrier_time_us` | Late-Arriving MPI Ranks & Collective Imbalance |
| **`mpi_p2p_traffic.bt`** | `libmpi.so` (`MPI_Send`, `MPI_Isend`, `MPI_Recv`, `MPI_Irecv`, `MPI_Sendrecv`) | `@tx_bytes`, `@rx_bytes`, `@msg_size_bytes`, `@p2p_calls` | Point-to-Point Message Volumes & Process Placement |
| **`openmp_hybrid_contention.bt`** | `libgomp.so.1` (`GOMP_barrier`, `GOMP_critical_start`, `GOMP_parallel`) | `@omp_barrier_wait_us`, `@omp_barrier_stats_us`, `@total_barrier_stall_us`, `@barrier_events`, `@critical_lock_wait_us`, `@critical_lock_stats_us`, `@critical_lock_calls`, `@parallel_region_dur_us`, `@parallel_region_stats_us`, `@parallel_regions_count` | Thread Load Imbalance & Lock Contention in Hybrid Ranks |
| **`cuda_sync_bubbles.bt`** | `libcudart.so` (`cudaStreamSynchronize`, `cudaDeviceSynchronize`, `cudaEventSynchronize`, `cudaMemcpy`) | `@sync_stall_us`, `@sync_stats_us`, `@sync_calls`, `@rank_sync_stall_us`, `@sync_memcpy_bytes` | CPU Host-GPU Stalls & Kernel Pipeline Bubbles |
| **`nccl_collective_skew.bt`** | `libnccl.so` (`ncclAllReduce`, `ncclReduceScatter`, `ncclAllGather`, `ncclBroadcast`) | `@nccl_latency_us`, `@nccl_stats_us`, `@rank_allreduce_us`, `@rank_allreduce_stats`, `@total_elements`, `@call_counts`, `@rank_reducescatter_us`, `@rank_allgather_us` | Distributed Multi-GPU AllReduce Skew & Ring Latency |

---

## 1. `ai_checkpoint_lustre.bt`: AI Checkpoint & Lustre OST Profiler

### Target Use Case
Large-scale PyTorch/Megatron-LM model checkpointing (saving multi-gigabyte state dictionaries) and high-throughput parallel I/O. Intercepts POSIX I/O calls, dynamically extracts the Lustre Object Storage Target (OST) index using the `lustre_ost(fd, offset)` helper, and profiles per-OST bandwidth and latency distributions.

### Intercepted Probes
- `uprobe:libc:pwrite64`, `uretprobe:libc:pwrite64`
- `uprobe:libc:write`, `uretprobe:libc:write`
- `uprobe:libc:fdatasync`, `uretprobe:libc:fdatasync`, `uprobe:libc:fsync`, `uretprobe:libc:fsync`

### Launch Command
```bash
# Standalone execution on example application
./bin/ubpftrace -c "./examples/apps/lustre_io_app" presets/ai_checkpoint_lustre.bt

# Multi-node Slurm job execution across 2 nodes (4 ranks)
srun -N 2 -n 4 -l ./bin/ubpftrace -c ./examples/apps/lustre_io_app presets/ai_checkpoint_lustre.bt
```

### Verified Multi-Node Execution Output
```text
0: Attached 9 probes
1: Attached 9 probes
2: Attached 9 probes
3: Attached 9 probes
0: [LustreApp] Target file opened: /pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/scratch_lustre_test.dat (fd=10)
0: [LustreApp] Wrote 1048576 bytes at offset 0 MB
0: [LustreApp] Done.

0: @ost_write_bytes[186]: 8388608
0: @ost_write_latency_us[186]:
0: [512, 1K)              2 |@@@@@@@@@@@@@@@@@@@@@@@@@@                          |
0: [1K, 2K)               4 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
0: [2K, 4K)               0 |                                                    |
0: [4K, 8K)               2 |@@@@@@@@@@@@@@@@@@@@@@@@@@                          |

0: @ost_write_ops[186]: 8
0: @stream_write_bytes: 568
0: @stream_write_latency_us:
0: [4, 8)                 1 |@@@@@@@@@@@@@                                       |
0: [128, 256)             4 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|

1: @ost_write_bytes[186]: 8388608
1: @ost_write_latency_us[186]:
1: [512, 1K)              1 |@@@@@@@@                                            |
1: [1K, 2K)               6 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
1: [2K, 4K)               1 |@@@@@@@@                                            |
1: @ost_write_ops[186]: 8
```

---

## 2. `mpi_straggler_detector.bt`: MPI Collective Straggler & Barrier Imbalance

### Target Use Case
Identifies ranks that arrive late to global collectives (`MPI_Barrier`, `MPI_Allreduce`, `MPI_Alltoall`, `MPI_Waitall`), causing all other ranks in the communicator to stall idle. Generates per-rank execution counts, latency log-histograms, and exact summary statistics.

### Intercepted Probes
- `uprobe:mpi:MPI_Barrier`, `uretprobe:mpi:MPI_Barrier`
- `uprobe:mpi:MPI_Allreduce`, `uretprobe:mpi:MPI_Allreduce`
- `uprobe:mpi:MPI_Alltoall`, `uretprobe:mpi:MPI_Alltoall`
- `uprobe:mpi:MPI_Waitall`, `uretprobe:mpi:MPI_Waitall`

### Launch Command
```bash
# Standalone execution (using Cray MPICH)
MPICH_GPU_SUPPORT_ENABLED=0 MPICH_COLL_OPT_OFF=1 ./bin/ubpftrace -c "./examples/apps/hpc_app" presets/mpi_straggler_detector.bt

# Multi-node Slurm job execution across 2 nodes (4 ranks)
srun -N 2 -n 4 -l bash -c "MPICH_GPU_SUPPORT_ENABLED=0 MPICH_COLL_OPT_OFF=1 ./bin/ubpftrace -c ./examples/apps/hpc_app presets/mpi_straggler_detector.bt"
```

### Verified Multi-Node Execution Output
```text
0: Attached 9 probes
1: Attached 9 probes
2: Attached 9 probes
3: Attached 9 probes
0: [Rank 0/4] HPC Worker started.
0: [Rank 0/4] Completed iterations. Global sum: 1275.00
1: [Rank 1/4] HPC Worker started.
1: [Rank 1/4] Completed iterations. Global sum: 1275.00
2: [Rank 2/4] HPC Worker started.
2: [Rank 2/4] Completed iterations. Global sum: 1275.00
3: [Rank 3/4] HPC Worker started.
3: [Rank 3/4] Completed iterations. Global sum: 1275.00

0: @allreduce_calls[0]: 3
0: @allreduce_stats_us[0]: { .count = 3, .average = 71, .total = 213 }
0: @barrier_calls[0]: 4
0: @barrier_stats_us[0]: { .count = 4, .average = 24, .total = 96 }
0: @barrier_wait_us[0]:
0: [8, 16)                3 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
0: [32, 64)               1 |@@@@@@@@@@@@@@@@@                                   |
0: @total_barrier_time_us: 96

1: @allreduce_calls[1]: 3
1: @allreduce_stats_us[1]: { .count = 3, .average = 74, .total = 223 }
1: @barrier_calls[1]: 4
1: @barrier_stats_us[1]: { .count = 4, .average = 6025, .total = 24103 }
1: @barrier_wait_us[1]:
1: [8, 16)                1 |@@@@@@@@@@@@@@@@@                                   |
1: [4K, 8K)               3 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
1: @total_barrier_time_us: 24103

2: @barrier_calls[2]: 4
2: @barrier_stats_us[2]: { .count = 4, .average = 6065, .total = 24261 }
2: @total_barrier_time_us: 24261

3: @barrier_calls[3]: 4
3: @barrier_stats_us[3]: { .count = 4, .average = 6053, .total = 24213 }
3: @total_barrier_time_us: 24213
```

*(Notice how Rank 0 straggles in compute, spending only 96 µs in barriers, while peer Ranks 1, 2, and 3 accumulate ~24,000 µs of idle barrier wait stalls).*

---

## 3. `mpi_p2p_traffic.bt`: Point-to-Point Communication Volume & Payload Sizes

### Target Use Case
Analyzes point-to-point communication volumes across synchronous and asynchronous primitives (`MPI_Send`, `MPI_Isend`, `MPI_Recv`, `MPI_Irecv`, `MPI_Sendrecv`). Records per-rank transmission (`@tx_bytes`) and reception (`@rx_bytes`) totals alongside payload size distribution histograms.

### Intercepted Probes
- `uprobe:mpi:MPI_Send`
- `uprobe:mpi:MPI_Isend`
- `uprobe:mpi:MPI_Recv`
- `uprobe:mpi:MPI_Irecv`
- `uprobe:mpi:MPI_Sendrecv`

### Launch Command
```bash
# Standalone execution on example application
MPICH_GPU_SUPPORT_ENABLED=0 MPICH_COLL_OPT_OFF=1 ./bin/ubpftrace -c "./examples/apps/mpi_p2p_app" presets/mpi_p2p_traffic.bt

# Multi-node Slurm job execution across 2 nodes (4 ranks)
srun -N 2 -n 4 -l bash -c "MPICH_GPU_SUPPORT_ENABLED=0 MPICH_COLL_OPT_OFF=1 ./bin/ubpftrace -c ./examples/apps/mpi_p2p_app presets/mpi_p2p_traffic.bt"
```

### Verified Multi-Node Execution Output
```text
0: Attached 6 probes
1: Attached 6 probes
2: Attached 6 probes
3: Attached 6 probes
0: [P2P App] Rank 0/4 starting P2P communication loops...
1: [P2P App] Rank 1/4 starting P2P communication loops...
2: [P2P App] Rank 2/4 starting P2P communication loops...
3: [P2P App] Rank 3/4 starting P2P communication loops...
0: [P2P App] Rank 0 finished all P2P exchanges.
1: [P2P App] Rank 1 finished all P2P exchanges.
2: [P2P App] Rank 2 finished all P2P exchanges.
3: [P2P App] Rank 3 finished all P2P exchanges.

0: @msg_size_bytes[MPI_Irecv]:
0: [1K, 2K)               4 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
0: @msg_size_bytes[MPI_Isend]:
0: [1K, 2K)               4 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
0: @msg_size_bytes[MPI_Recv]:
0: [2K, 4K)               4 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
0: @msg_size_bytes[MPI_Send]:
0: [2K, 4K)               4 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
0: @msg_size_bytes[MPI_Sendrecv]:
0: [2K, 4K)               4 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
0: @p2p_calls[MPI_Sendrecv]: 4
0: @p2p_calls[MPI_Irecv]: 4
0: @p2p_calls[MPI_Recv]: 4
0: @p2p_calls[MPI_Isend]: 4
0: @p2p_calls[MPI_Send]: 4
0: @rx_bytes[0]: 12288
0: @tx_bytes[0]: 20480
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
# Standalone execution with 4 OpenMP threads
OMP_NUM_THREADS=4 ./bin/ubpftrace -c "./examples/apps/omp_app" presets/openmp_hybrid_contention.bt

# Multi-node hybrid execution across 2 nodes (2 ranks, 4 threads per rank)
srun -N 2 -n 2 -c 4 -l bash -c "OMP_NUM_THREADS=4 ./bin/ubpftrace -c ./examples/apps/omp_app presets/openmp_hybrid_contention.bt"
```

### Verified Multi-Node Execution Output
```text
0: Attached 7 probes
1: Attached 7 probes
0: [OpenMP App] Starting OpenMP test with 4 threads...
0: [OpenMP App] Completed parallel sections. shared_counter=40
1: [OpenMP App] Starting OpenMP test with 4 threads...
1: [OpenMP App] Completed parallel sections. shared_counter=40

0: @barrier_events[1600379]: 4
0: @barrier_events[1600391]: 4
0: @barrier_events[1600393]: 4
0: @barrier_events[1600392]: 4
0: @critical_lock_calls[0]: 16
0: @critical_lock_stats_us[0]: { .count = 16, .average = 3088, .total = 49411 }
0: @critical_lock_wait_us[0]:
0: [0]                    3 |@@@@@@@@@@@@@@@@@@@                                 |
0: [4, 8)                 1 |@@@@@@                                              |
0: [2K, 4K)               4 |@@@@@@@@@@@@@@@@@@@@@@@@@@                          |
0: [4K, 8K)               8 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|

0: @parallel_region_dur_us[0]:
0: [32K, 64K)             1 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
0: @parallel_region_stats_us[0]: { .count = 1, .average = 33591, .total = 33591 }
0: @parallel_regions_count[0]: 1
0: @total_barrier_stall_us[0]: 37248

1: @parallel_region_dur_us[1]:
1: [32K, 64K)             1 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
1: @parallel_region_stats_us[1]: { .count = 1, .average = 33621, .total = 33621 }
1: @parallel_regions_count[1]: 1
1: @total_barrier_stall_us[1]: 37143
```

---

## 5. `cuda_sync_bubbles.bt`: CUDA Host-Device Synchronization Bubbles

### Target Use Case
Detects excessive host CPU stalls caused by synchronous CUDA Runtime operations (`cudaStreamSynchronize`, `cudaDeviceSynchronize`, `cudaEventSynchronize`, `cudaMemcpy`). Pinpoints synchronous serialization bubbles that prevent overlapping compute kernels with data movement.

### Intercepted Probes
- `uprobe:cudart:cudaStreamSynchronize`, `uretprobe:cudart:cudaStreamSynchronize`
- `uprobe:cudart:cudaDeviceSynchronize`, `uretprobe:cudart:cudaDeviceSynchronize`
- `uprobe:cudart:cudaEventSynchronize`, `uretprobe:cudart:cudaEventSynchronize`
- `uprobe:cudart:cudaMemcpy`, `uretprobe:cudart:cudaMemcpy`

### Launch Command
```bash
# Standalone execution on example application
./bin/ubpftrace -c "./examples/apps/cuda_sync_app" presets/cuda_sync_bubbles.bt

# Multi-node multi-GPU Slurm execution across 2 GPU nodes
srun -N 2 -n 2 --gpus-per-node=1 -l ./bin/ubpftrace -c ./examples/apps/cuda_sync_app presets/cuda_sync_bubbles.bt
```

### Verified Multi-Node Execution Output
```text
0: Attached 9 probes
1: Attached 9 probes
0: [CUDA App] Starting CUDA synchronization and memory test...
0: [CUDA App] Finished CUDA synchronization calls.
1: [CUDA App] Starting CUDA synchronization and memory test...
1: [CUDA App] Finished CUDA synchronization calls.

0: @rank_sync_stall_us[0]: 371190
0: @sync_calls[cudaStreamSynchronize]: 4
0: @sync_calls[cudaMemcpy_Sync]: 4
0: @sync_calls[cudaEventSynchronize]: 4
0: @sync_calls[cudaDeviceSynchronize]: 4
0: @sync_memcpy_bytes: 4194304
0: @sync_stall_us[cudaMemcpy_Sync]:
0: [32, 64)               2 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
0: [64, 128)              1 |@@@@@@@@@@@@@@@@@@@@@@@@@@                          |
0: [256K, 512K)           1 |@@@@@@@@@@@@@@@@@@@@@@@@@@                          |
0: @sync_stats_us[cudaDeviceSynchronize]: { .count = 4, .average = 3, .total = 15 }
0: @sync_stats_us[cudaStreamSynchronize]: { .count = 4, .average = 5, .total = 20 }
0: @sync_stats_us[cudaEventSynchronize]: { .count = 4, .average = 10, .total = 43 }
0: @sync_stats_us[cudaMemcpy_Sync]: { .count = 4, .average = 92778, .total = 371112 }

1: @rank_sync_stall_us[1]: 312508
1: @sync_calls[cudaStreamSynchronize]: 4
1: @sync_calls[cudaMemcpy_Sync]: 4
1: @sync_calls[cudaEventSynchronize]: 4
1: @sync_calls[cudaDeviceSynchronize]: 4
1: @sync_memcpy_bytes: 4194304
1: @sync_stats_us[cudaMemcpy_Sync]: { .count = 4, .average = 78110, .total = 312443 }
```

---

## 6. `nccl_collective_skew.bt`: Multi-GPU NCCL Collective Skew & Ring Latency

### Target Use Case
Monitors Distributed Deep Learning frameworks (Megatron-LM, DeepSpeed, PyTorch DDP/FSDP). Profiles collective synchronization latencies (`ncclAllReduce`, `ncclReduceScatter`, `ncclAllGather`, `ncclBroadcast`) across GPU ranks, tracking tensor element volume and identifying straggler GPU ranks caused by PCIe/NVLink degradation or thermal throttling.

### Intercepted Probes
- `uprobe:nccl:ncclAllReduce`, `uretprobe:nccl:ncclAllReduce`
- `uprobe:nccl:ncclReduceScatter`, `uretprobe:nccl:ncclReduceScatter`
- `uprobe:nccl:ncclAllGather`, `uretprobe:nccl:ncclAllGather`
- `uprobe:nccl:ncclBroadcast`, `uretprobe:nccl:ncclBroadcast`

### Launch Command
```bash
# Standalone execution on example application
LD_LIBRARY_PATH=/pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/examples/apps:$LD_LIBRARY_PATH \
./bin/ubpftrace -c "./examples/apps/nccl_collective_app" presets/nccl_collective_skew.bt

# Multi-node multi-GPU Slurm execution across 2 GPU nodes
srun -N 2 -n 2 --gpus-per-node=1 -l bash -c \
  "LD_LIBRARY_PATH=/pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/examples/apps:\$LD_LIBRARY_PATH ./bin/ubpftrace -c ./examples/apps/nccl_collective_app presets/nccl_collective_skew.bt"
```

### Verified Multi-Node Execution Output
```text
0: Attached 9 probes
1: Attached 9 probes
0: [NCCL App] Starting distributed AI collective communication loops...
0: [NCCL App] Finished all collective operations.
1: [NCCL App] Starting distributed AI collective communication loops...
1: [NCCL App] Finished all collective operations.

0: @call_counts[AllGather]: 4
0: @call_counts[AllReduce]: 4
0: @call_counts[ReduceScatter]: 4
0: @call_counts[Broadcast]: 4
0: @nccl_latency_us[AllGather]:
0: [2K, 4K)               4 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
0: @nccl_latency_us[AllReduce]:
0: [2K, 4K)               4 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
0: @nccl_latency_us[Broadcast]:
0: [1K, 2K)               4 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
0: @nccl_latency_us[ReduceScatter]:
0: [2K, 4K)               4 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|

0: @nccl_stats_us[Broadcast]: { .count = 4, .average = 1057, .total = 4230 }
0: @nccl_stats_us[ReduceScatter]: { .count = 4, .average = 2058, .total = 8232 }
0: @nccl_stats_us[AllGather]: { .count = 4, .average = 2558, .total = 10232 }
0: @nccl_stats_us[AllReduce]: { .count = 4, .average = 3061, .total = 12247 }
0: @rank_allgather_stats[0]: { .count = 4, .average = 2558, .total = 10232 }
0: @rank_allreduce_stats[0]: { .count = 4, .average = 3061, .total = 12247 }
0: @rank_reducescatter_stats[0]: { .count = 4, .average = 2058, .total = 8232 }
0: @total_elements[AllGather]: 1048576
0: @total_elements[ReduceScatter]: 1048576
0: @total_elements[AllReduce]: 4194304
0: @total_elements[Broadcast]: 4194304

1: @rank_allgather_stats[1]: { .count = 4, .average = 2558, .total = 10233 }
1: @rank_allreduce_stats[1]: { .count = 4, .average = 3063, .total = 12254 }
1: @rank_reducescatter_stats[1]: { .count = 4, .average = 2060, .total = 8240 }
```
