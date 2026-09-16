#!/bin/bash
set -e

# ==============================================================================
# Automated Overhead Evaluation Suite for ubpftrace
# Runs Baseline vs Traced benchmarks across 6 scenarios + microbenchmark (K=5)
# ==============================================================================

WORK_DIR="/pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace"
cd "$WORK_DIR"

export LD_LIBRARY_PATH="$WORK_DIR/examples/apps:$LD_LIBRARY_PATH"
export MPICH_COLL_OPT_OFF=1
export MPICH_GPU_SUPPORT_ENABLED=0

echo "========================================================================"
echo "          UBPFTRACE OVERHEAD EVALUATION (NERSC PERLMUTTER)              "
echo "========================================================================"

# Helper to run command K times and compute mean latency and wall time
run_bench() {
    local name="$1"
    local cmd_base="$2"
    local cmd_traced="$3"
    local reps=5

    echo ""
    echo "------------------------------------------------------------------------"
    echo ">>> Running Benchmark: $name (K=$reps repetitions)"
    echo "------------------------------------------------------------------------"

    echo ">> [1/2] Running BASELINE (Unprofiled)..."
    for i in $(seq 1 $reps); do
        echo -n "   Run $i: "
        eval "$cmd_base"
    done

    echo ">> [2/2] Running TRACED (ubpftrace active)..."
    for i in $(seq 1 $reps); do
        echo -n "   Run $i: "
        eval "$cmd_traced"
    done
}

# 1. Microbenchmark: Raw Probe Overhead
cat << 'EOF' > /tmp/micro_uprobe.bt
uprobe:/pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/examples/benchmarks/bench_micro_probe:target_probe_func {
    @calls = count();
}
EOF

cat << 'EOF' > /tmp/micro_uretprobe.bt
uprobe:/pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/examples/benchmarks/bench_micro_probe:target_probe_func {
    @start[tid] = nsecs;
}
uretprobe:/pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/examples/benchmarks/bench_micro_probe:target_probe_func /@start[tid]/ {
    $dur = nsecs - @start[tid];
    @lat = hist($dur);
    @stats = stats($dur);
    delete(@start[tid]);
}
EOF

echo ""
echo "=== [Phase 0] Microbenchmark: Isolated Probe Invocation Cost ==="
echo ">> Baseline (Uninstrumented function):"
./examples/benchmarks/bench_micro_probe 1000000

echo ">> Traced with Entry uprobe only (@calls = count()):"
./bin/ubpftrace -c "./examples/benchmarks/bench_micro_probe 1000000" /tmp/micro_uprobe.bt 2>&1 | grep -E "MicroBench|calls"

echo ">> Traced with uprobe + uretprobe + hist() + stats():"
./bin/ubpftrace -c "./examples/benchmarks/bench_micro_probe 1000000" /tmp/micro_uretprobe.bt 2>&1 | grep -E "MicroBench|stats"

# 2. Scenario 1: Lustre I/O Checkpointing
run_bench "Scenario 1: Lustre Striped I/O (pwrite64 + fdatasync + lustre_ost)" \
    "./examples/benchmarks/bench_lustre_io 2000" \
    "./bin/ubpftrace -c './examples/benchmarks/bench_lustre_io 2000' presets/ai_checkpoint_lustre.bt 2>&1 | grep 'LustreBench'"

# 3. Scenario 2: MPI Point-to-Point Traffic
run_bench "Scenario 2: MPI Point-to-Point Communication (MPI_Send/Recv 10K msgs)" \
    "srun -N 2 -n 4 ./examples/benchmarks/bench_mpi_p2p 10000" \
    "srun -N 2 -n 4 ./bin/ubpftrace -c './examples/benchmarks/bench_mpi_p2p 10000' presets/mpi_p2p_traffic.bt 2>&1 | grep 'MPI-P2P-Bench' || true"

# 4. Scenario 3: MPI Collective Synchronization
run_bench "Scenario 3: MPI Collective Barrier & Allreduce (5K cycles)" \
    "srun -N 2 -n 4 ./examples/benchmarks/bench_mpi_collectives 5000" \
    "srun -N 2 -n 4 ./bin/ubpftrace -c './examples/benchmarks/bench_mpi_collectives 5000' presets/mpi_straggler_detector.bt 2>&1 | grep 'MPI-Collective-Bench' || true"

# 5. Scenario 4: OpenMP Thread Synchronization
run_bench "Scenario 4: OpenMP Thread Contention (16 Threads x 10K cycles)" \
    "OMP_NUM_THREADS=16 ./examples/benchmarks/bench_omp 10000" \
    "OMP_NUM_THREADS=16 ./bin/ubpftrace -c './examples/benchmarks/bench_omp 10000' presets/openmp_hybrid_contention.bt 2>&1 | grep 'OMP-Bench'"

# 6. Scenario 5: CUDA Synchronous Runtime
run_bench "Scenario 5: CUDA Host-Device Synchronization (5K cycles)" \
    "./examples/benchmarks/bench_cuda 5000" \
    "./bin/ubpftrace -c './examples/benchmarks/bench_cuda 5000' presets/cuda_sync_bubbles.bt 2>&1 | grep 'CUDA-Bench'"

# 7. Scenario 6: NCCL Distributed Collectives
run_bench "Scenario 6: NCCL Distributed Collectives (5K cycles)" \
    "srun -N 2 -n 8 ./examples/benchmarks/bench_nccl 5000" \
    "srun -N 2 -n 8 ./bin/ubpftrace -c './examples/benchmarks/bench_nccl 5000' presets/nccl_collective_skew.bt 2>&1 | grep 'NCCL-Bench' || true"

echo ""
echo "========================================================================"
echo "                   OVERHEAD EVALUATION COMPLETE                         "
echo "========================================================================"
