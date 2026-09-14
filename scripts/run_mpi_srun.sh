#!/usr/bin/env bash
set -e

# ==============================================================================
# ubpftrace Multi-Node HPC MPI Dynamic Tracing Launcher
# ==============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UBPFTRACE="${SCRIPT_DIR}/bin/ubpftrace"
APPS_DIR="${SCRIPT_DIR}/examples/apps"
BT_SCRIPT="${SCRIPT_DIR}/examples/mpi_bottleneck.bt"

# Ensure hpc_app is built
echo "[Setup] Compiling MPI benchmark application (hpc_app)..."
make -C "${APPS_DIR}" hpc_app

NUM_NODES="${1:-2}"
NUM_TASKS="${2:-2}"
TIME_LIMIT="${3:-00:05:00}"

echo "============================================================"
echo "  UBPFTRACE MULTI-NODE MPI TRACING (srun / Slurm)"
echo "============================================================"
echo "Nodes: ${NUM_NODES} | MPI Tasks: ${NUM_TASKS} | Time: ${TIME_LIMIT}"
echo ""

if [ -n "${SLURM_JOB_ID}" ]; then
    echo "[Execution] Running inside active Slurm allocation (Job ID: ${SLURM_JOB_ID})..."
    srun -N "${NUM_NODES}" -n "${NUM_TASKS}" "${UBPFTRACE}" -c "${APPS_DIR}/hpc_app" "${BT_SCRIPT}"
else
    echo "[Allocation] Requesting ${NUM_NODES} interactive CPU nodes via salloc..."
    salloc -N "${NUM_NODES}" -C cpu -q interactive -t "${TIME_LIMIT}" -- \
        srun -N "${NUM_NODES}" -n "${NUM_TASKS}" "${UBPFTRACE}" -c "${APPS_DIR}/hpc_app" "${BT_SCRIPT}"
fi
