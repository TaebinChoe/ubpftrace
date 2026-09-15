# Flagship Production Presets Catalog

`ubpftrace` includes six pre-built, production-validated tracing scripts located in the [`presets/`](file:///pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/presets/) directory. These scripts target common performance bottlenecks in Large-Scale HPC, Distributed AI Training, Parallel Storage, and Multi-GPU Computing.

---

## Catalog Overview

```
presets/
├── ai_checkpoint_lustre.bt     # AI Model Checkpointing & Lustre OST Bandwidth
├── mpi_straggler_detector.bt   # MPI Collective Synchronization Skews & Stragglers
├── mpi_p2p_traffic.bt          # Point-to-Point MPI Message Volumes & Matrix Heatmaps
├── openmp_hybrid_contention.bt # OpenMP Barrier Wait & Critical Section Contention
├── cuda_sync_bubbles.bt        # CUDA Host-Device Synchronization Bubbles
└── nccl_collective_skew.bt     # Multi-GPU NCCL AllReduce Skew & Ring Latency
```

---

## 1. `ai_checkpoint_lustre.bt`: AI Checkpoint & Lustre OST Profiler

### Target Use Case
Large-scale PyTorch/Megatron-LM model checkpointing (e.g. saving 100GB+ LLM state dictionaries). Detects overloaded Lustre Object Storage Targets (OSTs), unaligned I/O, and file write latency bottlenecks.

### Intercepted Probes
- `uprobe:libc.so.6:write`, `uprobe:libc.so.6:pwrite64`, `uprobe:libc.so.6:writev`
- Resolves backing Lustre OST target index via the `lustre_ost` builtin.

### Launch Command
```bash
srun -N 4 -n 16 ./bin/ubpftrace \
  -c "python3 train_llm.py --save-checkpoint" \
  ./presets/ai_checkpoint_lustre.bt
```

### Metrics Produced
```text
@total_checkpoint_bytes: 107,374,182,400 (100.00 GB)

@bytes_per_ost:
[OST 12]    28,450,192,384 bytes (26.5 GB)  <-- Overloaded OST target!
[OST 14]     8,192,400,000 bytes ( 7.6 GB)
[OST 15]     8,192,400,000 bytes ( 7.6 GB)

@write_latency_us:
[100, 200)           14,200 |@@@@@@@@@@@@@@@@                        |
[200, 400)           28,400 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@        |
[10000, 20000)          120 |@                                       | (OST write stall)
```

---

## 2. `mpi_straggler_detector.bt`: MPI Collective Straggler Detector

### Target Use Case
Identifies ranks that arrive late to global collectives (`MPI_Barrier`, `MPI_Allreduce`, `MPI_Bcast`), causing all other ranks to waste compute cycles waiting.

### Intercepted Probes
- `uprobe:libmpi.so:MPI_Barrier`, `uprobe:libmpi.so:MPI_Allreduce`, `uprobe:libmpi.so:MPI_Bcast`

### Launch Command
```bash
srun -N 8 -n 64 ./bin/ubpftrace \
  -c "./bin/weather_forecast_sim" \
  ./presets/mpi_straggler_detector.bt
```

### Metrics Produced
```text
@barrier_wait_time_ms:
[Rank 0]       12 ms
[Rank 1]       14 ms
...
[Rank 47]     890 ms  <-- STRAGGLER RANK! (Slow computation before barrier)

@collective_imbalance_hist:
[0, 10)            14,020 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@        |
[10, 50)            1,200 |@@@                                     |
[500, 1000)            42 |@                                       |
```

---

## 3. `mpi_p2p_traffic.bt`: Point-to-Point Communication Volume & Heatmaps

### Target Use Case
Analyzes nearest-neighbor vs. cross-torus point-to-point MPI communication (`MPI_Send`, `MPI_Isend`, `MPI_Recv`). Generates communication matrices to identify unoptimized process placement.

### Intercepted Probes
- `uprobe:libmpi.so:MPI_Send`, `uprobe:libmpi.so:MPI_Isend`, `uprobe:libmpi.so:MPI_Recv`

### Launch Command
```bash
srun -N 4 -n 32 ./bin/ubpftrace \
  -c "./bin/cfd_fluid_solver" \
  ./presets/mpi_p2p_traffic.bt
```

### Metrics Produced
```text
@p2p_matrix_bytes[src_rank, dst_rank]:
[Rank 0, Rank 1]:    1,048,576,000 bytes (Intra-node high bandwidth)
[Rank 0, Rank 31]:      10,240,000 bytes (Inter-node high latency message)

@p2p_message_size_dist:
[64, 128)             80,400 |@@@@@@@@@@@@@@@@@@@@@@@@                |
[65536, 131072)       32,100 |@@@@@@@@@@                              |
```

---

## 4. `openmp_hybrid_contention.bt`: OpenMP Hybrid Barrier & Lock Contention

### Target Use Case
Profiles multi-threaded CPU OpenMP regions in hybrid MPI+OpenMP codes. Pinpoints thread synchronization overhead, unbalanced `omp parallel for` iterations, and critical section locks.

### Intercepted Probes
- `uprobe:libgomp.so.1:GOMP_barrier`, `uprobe:libgomp.so.1:GOMP_critical_start`

### Launch Command
```bash
export OMP_NUM_THREADS=16
srun -N 2 -n 4 --cpus-per-task=16 ./bin/ubpftrace \
  -c "./bin/hybrid_molecular_dynamics" \
  ./presets/openmp_hybrid_contention.bt
```

### Metrics Produced
```text
@omp_barrier_wait_us[Rank, TID]:
[Rank 0, TID 1024]:     420 us
[Rank 0, TID 1025]:   8,920 us  <-- Thread load imbalance!

@critical_section_contention_count[Rank]:
[Rank 0]:   1,420,000 lock acquisitions (Heavy lock contention)
[Rank 1]:   1,418,900 lock acquisitions
```

---

## 5. `cuda_sync_bubbles.bt`: CUDA Host-Device Synchronization Bubbles

### Target Use Case
Detects excessive CPU-GPU synchronization calls (`cudaDeviceSynchronize`, `cudaStreamSynchronize`, `cudaMemcpy`) that stall CPU compute threads and prevent GPU kernel pipelining.

### Intercepted Probes
- `uprobe:libcudart.so:cudaDeviceSynchronize`, `uprobe:libcudart.so:cudaStreamSynchronize`, `uprobe:libcudart.so:cudaMemcpy`

### Launch Command
```bash
./bin/ubpftrace -c "python3 train_diffusion_model.py" \
  ./presets/cuda_sync_bubbles.bt
```

### Metrics Produced
```text
@cuda_sync_calls[comm]:
[cudaDeviceSynchronize]:   14,200 calls (Blocking sync bubble!)
[cudaStreamSynchronize]:    2,100 calls

@cuda_sync_duration_us:
[100, 200)             8,400 |@@@@@@@@@@@@@@@@                        |
[500, 1000)            4,200 |@@@@@@@@                                |
[2000, 5000)           1,100 |@@                                      |
```

---

## 6. `nccl_collective_skew.bt`: Multi-GPU NCCL Collective Skew & Ring Latency

### Target Use Case
Monitors Multi-GPU Distributed Deep Learning (Megatron-LM, DeepSpeed, FSDP). Profiles `ncclAllReduce` and `ncclAllGather` communication latencies and detects GPU clock throttling or PCIe/NVLink degradation.

### Intercepted Probes
- `uprobe:libnccl.so:ncclAllReduce`, `uprobe:libnccl.so:ncclAllGather`

### Launch Command
```bash
srun -N 4 --gpus-per-node=4 ./bin/ubpftrace \
  -c "torchrun --nproc_per_node=4 train_transformer.py" \
  ./presets/nccl_collective_skew.bt
```

### Metrics Produced
```text
@nccl_allreduce_bytes[Rank]:
[Rank 0]:   4,294,967,296 bytes (4.0 GB)
[Rank 1]:   4,294,967,296 bytes (4.0 GB)

@nccl_allreduce_latency_us:
[50, 100)             42,000 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@        |
[100, 200)             3,400 |@@@                                     |
[5000, 10000)             12 |@                                       | (NVLink throttle skew)
```
