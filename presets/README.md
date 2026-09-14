# `ubpftrace` Production Presets Suite

This directory contains curated, production-ready `.bt` preset scripts for diagnosing performance bottlenecks across **Traditional HPC Simulations** and **Large-Scale Distributed AI / LLM Training** workloads.

---

## 📋 Catalog of Presets

| Preset Script | Primary Domain | Target Bottleneck Diagnosed |
| :--- | :--- | :--- |
| [`ai_checkpoint_lustre.bt`](ai_checkpoint_lustre.bt) | Distributed AI / Lustre I/O | Multi-terabyte checkpoint write stalls & degraded Lustre OST identification via `lustre_ost()`. |
| [`nccl_collective_skew.bt`](nccl_collective_skew.bt) | Distributed AI (NCCL) | AllReduce/ReduceScatter/AllGather tail latencies and GPU rank stragglers in LLM training. |
| [`cuda_sync_bubbles.bt`](cuda_sync_bubbles.bt) | GPU Runtime / PyTorch | Silent blocking CPU-GPU synchronizations (`cudaStreamSynchronize`, `.item()`) creating GPU bubbles. |
| [`mpi_straggler_detector.bt`](mpi_straggler_detector.bt) | Traditional HPC (MPI) | Computational load imbalance, barrier stalls, and async request wait times across simulation ranks. |
| [`openmp_hybrid_contention.bt`](openmp_hybrid_contention.bt) | Hybrid HPC (OpenMP) | Thread load imbalance at loop barriers and critical section lock contention inside hybrid ranks. |
| [`mpi_p2p_traffic.bt`](mpi_p2p_traffic.bt) | Traditional HPC (MPI) | Message size distributions (Eager vs. Rendezvous) and per-rank Tx/Rx communication volume. |

---

## 🚀 Quick Usage Guide

All presets run in **pure userspace (Ring 3)** with **zero root privileges**.

### 1. AI Model Checkpoint & Lustre OST Bottleneck Profiling
Tracks per-OST latency distributions during checkpoint saves (Safetensors / PyTorch Distributed Snapshot):

```bash
# Run locally or under Slurm
ubpftrace -c "python train_fsdp.py" presets/ai_checkpoint_lustre.bt

# Multi-node Slurm job (e.g. 4 nodes, 16 ranks on Perlmutter)
srun -N 4 -n 16 ubpftrace -c "python train_fsdp.py" presets/ai_checkpoint_lustre.bt
```

#### Optimization Insight:
* If `@ost_write_latency_us[X]` exhibits a long tail for a specific OST ID (e.g., OST 74), that storage target is congested or degraded.
* Use `lfs setstripe -c <N> -S <size>` to redistribute stripes away from overloaded targets or increase striping parallelism.

---

### 2. Distributed AI NCCL Collective Skew & Straggler Detection
Measures collective tail latencies (`AllReduce`, `ReduceScatter`, `AllGather`, `Broadcast`) across GPU ranks:

```bash
# Tracing PyTorch DDP / FSDP / Megatron-LM training
srun -N 2 -n 8 ubpftrace -c "python train_llm.py" presets/nccl_collective_skew.bt
```

#### Optimization Insight:
* Look at `@rank_allreduce_us[rank]` to find ranks with high average or tail latency.
* Check if slowest ranks correspond to specific physical nodes (indicating network interface packet loss, thermal throttling, or NUMA PCIe bandwidth degradation).

---

### 3. CUDA Host-Device Synchronization Bubbles
Detects blocking calls (`cudaStreamSynchronize`, `cudaDeviceSynchronize`, `cudaEventSynchronize`, synchronous `cudaMemcpy`) that drain GPU work queues:

```bash
ubpftrace -c "python train.py" presets/cuda_sync_bubbles.bt
```

#### Optimization Insight:
* Cumulative stall time in `@rank_sync_stall_us[rank]` reveals GPU pipeline starvation.
* Replace blocking scalar extractions (`loss.item()`, `.cpu()`) in inner training loops with non-blocking async logging or delayed synchronization.

---

### 4. MPI Simulation Collective & Straggler Detector
Profiles barrier synchronization stalls and collective reduction wait times across thousands of HPC simulation ranks:

```bash
srun -N 4 -n 64 ubpftrace -c "./nek5000" presets/mpi_straggler_detector.bt
```

#### Optimization Insight:
* Ranks reporting near-zero `@barrier_wait_us` are the computational stragglers (arriving late).
* Ranks reporting high `@barrier_wait_us` are fast ranks wasting CPU cycles waiting for stragglers. Adjust domain decomposition subgrid sizes to rebalance computation.

---

### 5. OpenMP Hybrid Thread Contention
Profiles thread synchronization stalls at OpenMP barriers and critical section mutex contention:

```bash
OMP_NUM_THREADS=16 srun -n 4 ubpftrace -c "./hybrid_sim" presets/openmp_hybrid_contention.bt
```

#### Optimization Insight:
* High `@omp_barrier_wait_us[tid]` variance indicates thread workload imbalance. Switch from `schedule(static)` to `schedule(guided)` or `schedule(dynamic)`.
* High `@critical_lock_wait_us` indicates serialized lock contention; refactor using atomic directives (`#pragma omp atomic`) or thread-private reduction arrays.

---

### 6. MPI Point-to-Point Traffic & Message Sizing
Analyzes message size distributions and per-rank transmission/reception volumes (optimized to avoid map cardinality explosion):

```bash
srun -N 4 -n 64 ubpftrace -c "./lammps" presets/mpi_p2p_traffic.bt
```

#### Optimization Insight:
* If `@msg_size_bytes["MPI_Send"]` is dominated by small buffers (< 4KB), network overhead will be high. Aggregate small messages into batched contiguous buffers.
* Compare message sizes against the MPI implementation's Eager/Rendezvous switch threshold (`MPICH_SMP_SINGLE_COPY_MODE`, `I_MPI_EAGER_THRESHOLD`) to tune protocol thresholds.
