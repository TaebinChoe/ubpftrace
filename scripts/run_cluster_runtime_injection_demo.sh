#!/usr/bin/env bash
# ==============================================================================
# Multi-Node Runtime Injection Interactive Demo
# Target Nodes: nid004146, nid004147
# ==============================================================================

set -e

ROOT_DIR="/pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace"
UBT_ATTACH="${ROOT_DIR}/bin/ubt-attach"
HPC_APP="${ROOT_DIR}/examples/apps/hpc_app"

# Automatically detect active Slurm Job ID if not provided
if [ -z "${JOBID}" ]; then
    JOBID=$(squeue -u $USER -h -o "%A" | head -n 1)
fi

if [ -z "${JOBID}" ]; then
    echo "Error: No active Slurm job found. Please specify JOBID manually (e.g. JOBID=58774223 ./run_cluster_runtime_injection_demo.sh)"
    exit 1
fi

echo "=============================================================================="
echo " Detected Active Slurm Job ID: ${JOBID}"
echo " Compute Nodes Assigned: $(squeue -j ${JOBID} -h -o '%N')"
echo "=============================================================================="

echo ""
echo ">>> Step 1: Launching 4-Rank Distributed MPI Simulation (300 Iterations)..."
srun --jobid="${JOBID}" -N 2 -n 4 "${HPC_APP}" 300 > /pscratch/sd/s/sgkim/tchoe_home/hpc_app_cluster.log 2>&1 &
APP_BG_PID=$!
echo "MPI Application running in background (PID: ${APP_BG_PID})."
echo "Allowing ranks 1.5s to initialize MPI communicators..."
sleep 1.5

echo ""
echo ">>> Step 2: Performing Cluster-Wide Dynamic Runtime Injection via ubt-attach..."
"${UBT_ATTACH}" --job "${JOBID}" --comm hpc_app

echo ""
echo ">>> Step 3: Simulation is actively executing with probes attached (tracing for 3 seconds)..."
sleep 3.0

echo ""
echo ">>> Step 4: Dynamically detaching probes from all ranks (restoring 100% native speed)..."
"${UBT_ATTACH}" --job "${JOBID}" --comm hpc_app --detach

echo ""
echo ">>> Step 5: Waiting for MPI simulation to complete naturally..."
wait "${APP_BG_PID}"

echo ""
echo "=============================================================================="
echo " >>> Execution Log from All 4 MPI Ranks Across Both Nodes:"
echo "=============================================================================="
cat /pscratch/sd/s/sgkim/tchoe_home/hpc_app_cluster.log

echo ""
echo "=============================================================================="
echo " SUCCESS: Experiment Completed Flawlessly!"
echo "=============================================================================="
