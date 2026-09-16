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
Large-scale PyTorch/Megatron-LM model checkpointing (saving multi-gigabyte state dictionaries) and high-throughput parallel I/O. Intercepts POSIX I/O calls, dynamically extracts the Lustre Object Storage Target (OST) index using the `lustre_ost(fd)` helper, and profiles per-OST bandwidth and latency distributions.

### Intercepted Probes
- `uprobe:c:pwrite64`, `uprobe:c:pwrite`, `uretprobe:c:pwrite64`, `uretprobe:c:pwrite`
- `uprobe:c:write`, `uretprobe:c:write`
- `uprobe:c:fdatasync`, `uretprobe:c:fdatasync`, `uprobe:c:fsync`, `uretprobe:c:fsync`

### Launch Command
```bash
# Standalone execution on example application
./bin/ubpftrace -c "./examples/apps/lustre_io_app" presets/ai_checkpoint_lustre.bt

# Multi-node Slurm job execution
srun -N 4 -n 16 ./bin/ubpftrace \
  -c "python3 train_llm.py --save-checkpoint" \
  ./presets/ai_checkpoint_lustre.bt
```

### Real Execution Output
```text
Attached 9 probes
[LustreApp] Target file opened: /pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/scratch_lustre_test.dat (fd=10)
[LustreApp] Wrote 1048576 bytes at offset 0 MB
[LustreApp] Wrote 1048576 bytes at offset 1 MB
[LustreApp] Wrote 1048576 bytes at offset 2 MB
[LustreApp] Wrote 1048576 bytes at offset 3 MB
[LustreApp] Wrote 1048576 bytes at offset 4 MB
[LustreApp] Wrote 1048576 bytes at offset 5 MB
[LustreApp] Wrote 1048576 bytes at offset 6 MB
[LustreApp] Wrote 1048576 bytes at offset 7 MB
[LustreApp] Done.

@ost_write_bytes[288]: 8388608
@ost_write_latency_us[288]:
[1K, 2K)               8 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|

@ost_write_ops[288]: 8
@stream_write_bytes: 1068
@stream_write_latency_us:
[8, 16)                6 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@        |
[16, 32)               7 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
[32, 64)               2 |@@@@@@@@@@@@@@                                      |
[64, 128)              0 |                                                    |
[128, 256)             0 |                                                    |
[256, 512)             1 |@@@@@@@                                             |
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
MPICH_GPU_SUPPORT_ENABLED=0 ./bin/ubpftrace -c "./examples/apps/hpc_app" presets/mpi_straggler_detector.bt

# Distributed Slurm deployment across 8 nodes
srun -N 8 -n 64 ./bin/ubpftrace \
  -c "./bin/weather_forecast_sim" \
  ./presets/mpi_straggler_detector.bt
```

### Real Execution Output
```text
Attached 9 probes
[Rank 0/1] HPC Worker started.
[Rank 0/1] Completed iterations. Global sum: 127.50

@allreduce_calls[0]: 3
@allreduce_latency_us[0]:
[2, 4)                 2 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
[4, 8)                 0 |                                                    |
[8, 16)                0 |                                                    |
[16, 32)               1 |@@@@@@@@@@@@@@@@@@@@@@@@@@                          |

@allreduce_stats_us[0]: { .count = 3, .average = 8, .total = 26 }
@barrier_calls[0]: 3
@barrier_stats_us[0]: { .count = 3, .average = 10, .total = 31 }
@barrier_wait_us[0]:
[2, 4)                 2 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
[4, 8)                 0 |                                                    |
[8, 16)                0 |                                                    |
[16, 32)               1 |@@@@@@@@@@@@@@@@@@@@@@@@@@                          |

@total_allreduce_time_us: 26
@total_barrier_time_us: 31
```

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
MPICH_GPU_SUPPORT_ENABLED=0 ./bin/ubpftrace -c "./examples/apps/mpi_p2p_app" presets/mpi_p2p_traffic.bt

# Multi-node HPC application run
srun -N 4 -n 32 ./bin/ubpftrace \
  -c "./bin/cfd_fluid_solver" \
  ./presets/mpi_p2p_traffic.bt
```

### Real Execution Output
```text
Attached 6 probes
[P2P App] Rank 0/1 starting P2P communication loops...
[P2P App] Rank 0 finished all P2P exchanges.

@msg_size_bytes[MPI_Irecv]:
[1K, 2K)               4 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|

@msg_size_bytes[MPI_Isend]:
[1K, 2K)               4 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|

@msg_size_bytes[MPI_Sendrecv]:
[2K, 4K)               4 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|

@p2p_calls[MPI_Sendrecv]: 4
@p2p_calls[MPI_Irecv]: 4
@p2p_calls[MPI_Isend]: 4
@rx_bytes[0]: 4096
@tx_bytes[0]: 12288
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
# Standalone execution with 4 threads
OMP_NUM_THREADS=4 ./bin/ubpftrace -c "./examples/apps/omp_app" presets/openmp_hybrid_contention.bt

# Hybrid MPI+OpenMP Slurm deployment
export OMP_NUM_THREADS=16
srun -N 2 -n 4 --cpus-per-task=16 ./bin/ubpftrace \
  -c "./bin/hybrid_molecular_dynamics" \
  ./presets/openmp_hybrid_contention.bt
```

### Real Execution Output
```text
Attached 7 probes
[OpenMP App] Starting OpenMP test with 4 threads...
[OpenMP App] Completed parallel sections. shared_counter=40

@barrier_events[487044]: 4
@barrier_events[487105]: 4
@barrier_events[487104]: 4
@barrier_events[487103]: 4
@critical_lock_calls[0]: 16
@critical_lock_stats_us[0]: { .count = 16, .average = 3099, .total = 49588 }
@critical_lock_wait_us[0]:
[1]                    3 |@@@@@@@@@@@@@@@@@@@                                 |
[2, 4)                 0 |                                                    |
[4, 8)                 1 |@@@@@@                                              |
[8, 16)                0 |                                                    |
[16, 32)               0 |                                                    |
[32, 64)               0 |                                                    |
[64, 128)              0 |                                                    |
[128, 256)             0 |                                                    |
[256, 512)             0 |                                                    |
[512, 1K)              0 |                                                    |
[1K, 2K)               1 |@@@@@@                                              |
[2K, 4K)               3 |@@@@@@@@@@@@@@@@@@@                                 |
[4K, 8K)               8 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|

@omp_barrier_stats_us[487103]: { .count = 4, .average = 4, .total = 19 }
@omp_barrier_stats_us[487044]: { .count = 4, .average = 1595, .total = 6383 }
@omp_barrier_stats_us[487105]: { .count = 4, .average = 3627, .total = 14510 }
@omp_barrier_stats_us[487104]: { .count = 4, .average = 4162, .total = 16651 }
@omp_barrier_wait_us[487044]:
[128, 256)             1 |@@@@@@@@@@@@@@@@@                                   |
[256, 512)             0 |                                                    |
[512, 1K)              0 |                                                    |
[1K, 2K)               0 |                                                    |
[2K, 4K)               3 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|

@parallel_region_dur_us[0]:
[32K, 64K)             1 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|

@parallel_region_stats_us[0]: { .count = 1, .average = 34130, .total = 34130 }
@parallel_regions_count[0]: 1
@total_barrier_stall_us[0]: 37563
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

# Multi-GPU training workload
srun -N 2 -n 8 ubpftrace -c "python3 train.py" presets/cuda_sync_bubbles.bt
```

### Real Execution Output
```text
Attached 9 probes
[CUDA App] Starting CUDA synchronization and memory test...
[CUDA App] Finished CUDA synchronization calls.

@rank_sync_stall_us[0]: 361857
@sync_calls[cudaStreamSynchronize]: 4
@sync_calls[cudaMemcpy_Sync]: 4
@sync_calls[cudaEventSynchronize]: 4
@sync_calls[cudaDeviceSynchronize]: 4
@sync_memcpy_bytes: 4194304
@sync_stall_us[cudaDeviceSynchronize]:
[2, 4)                 3 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
[4, 8)                 0 |                                                    |
[8, 16)                1 |@@@@@@@@@@@@@@@@@                                   |

@sync_stall_us[cudaEventSynchronize]:
[2, 4)                 1 |@@@@@@@@@@@@@@@@@@@@@@@@@@                          |
[4, 8)                 2 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
[8, 16)                0 |                                                    |
[16, 32)               0 |                                                    |
[32, 64)               1 |@@@@@@@@@@@@@@@@@@@@@@@@@@                          |

@sync_stall_us[cudaMemcpy_Sync]:
[64, 128)              3 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
[128, 256)             0 |                                                    |
[256, 512)             0 |                                                    |
[512, 1K)              0 |                                                    |
[1K, 2K)               0 |                                                    |
[2K, 4K)               0 |                                                    |
[4K, 8K)               0 |                                                    |
[8K, 16K)              0 |                                                    |
[16K, 32K)             0 |                                                    |
[32K, 64K)             0 |                                                    |
[64K, 128K)            0 |                                                    |
[128K, 256K)           0 |                                                    |
[256K, 512K)           1 |@@@@@@@@@@@@@@@@@                                   |

@sync_stall_us[cudaStreamSynchronize]:
[2, 4)                 3 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
[4, 8)                 0 |                                                    |
[8, 16)                0 |                                                    |
[16, 32)               1 |@@@@@@@@@@@@@@@@@                                   |

@sync_stats_us[cudaDeviceSynchronize]: { .count = 4, .average = 5, .total = 20 }
@sync_stats_us[cudaStreamSynchronize]: { .count = 4, .average = 8, .total = 32 }
@sync_stats_us[cudaEventSynchronize]: { .count = 4, .average = 16, .total = 67 }
@sync_stats_us[cudaMemcpy_Sync]: { .count = 4, .average = 90434, .total = 361738 }
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
./bin/ubpftrace -c "./examples/apps/nccl_collective_app" presets/nccl_collective_skew.bt

# Multi-node PyTorch / torchrun distributed training
srun -N 4 --gpus-per-node=4 ./bin/ubpftrace \
  -c "torchrun --nproc_per_node=4 train_transformer.py" \
  ./presets/nccl_collective_skew.bt
```

### Real Execution Output
```text
Attached 9 probes
[NCCL App] Starting distributed AI collective communication loops...
[NCCL App] Finished all collective operations.

@call_counts[AllGather]: 4
@call_counts[AllReduce]: 4
@call_counts[ReduceScatter]: 4
@call_counts[Broadcast]: 4
@nccl_latency_us[AllGather]:
[2K, 4K)               4 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|

@nccl_latency_us[AllReduce]:
[2K, 4K)               4 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|

@nccl_latency_us[Broadcast]:
[1K, 2K)               4 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|

@nccl_latency_us[ReduceScatter]:
[2K, 4K)               4 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|

@nccl_stats_us[Broadcast]: { .count = 4, .average = 1061, .total = 4246 }
@nccl_stats_us[ReduceScatter]: { .count = 4, .average = 2063, .total = 8253 }
@nccl_stats_us[AllGather]: { .count = 4, .average = 2562, .total = 10248 }
@nccl_stats_us[AllReduce]: { .count = 4, .average = 3071, .total = 12286 }
@rank_allgather_stats[0]: { .count = 4, .average = 2562, .total = 10248 }
@rank_allgather_us[0]:
[2K, 4K)               4 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|

@rank_allreduce_stats[0]: { .count = 4, .average = 3071, .total = 12286 }
@rank_allreduce_us[0]:
[2K, 4K)               4 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|

@rank_reducescatter_stats[0]: { .count = 4, .average = 2063, .total = 8253 }
@rank_reducescatter_us[0]:
[2K, 4K)               4 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|

@total_elements[AllGather]: 1048576
@total_elements[ReduceScatter]: 1048576
@total_elements[AllReduce]: 4194304
@total_elements[Broadcast]: 4194304
```
