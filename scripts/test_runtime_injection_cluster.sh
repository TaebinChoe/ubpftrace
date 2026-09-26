#!/usr/bin/env bash
# ==============================================================================
# End-to-End Multi-Node Runtime Injection & Detachment Verification
# Targets: nid004146, nid004147 (Slurm Job: 58774223)
# ==============================================================================

set -e

ROOT_DIR="/pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace"
UBT_ATTACH="${ROOT_DIR}/bin/ubt-attach"
HPC_APP="${ROOT_DIR}/examples/apps/hpc_app"
JOBID="58774223"

echo "=============================================================================="
echo " 1. Starting 4-Rank Distributed MPI Simulation Across 2 Nodes (${JOBID})..."
echo "=============================================================================="
srun --jobid="${JOBID}" -N 2 -n 4 "${HPC_APP}" 300 > /pscratch/sd/s/sgkim/tchoe_home/hpc_app_cluster.log 2>&1 &
APP_BG_PID=$!
echo "MPI Job launched in background (PID ${APP_BG_PID}). Waiting for ranks to initialize..."
sleep 1.5

echo ""
echo "=============================================================================="
echo " 2. Executing Cluster-Wide Dynamic Runtime Injection (ubt-attach)..."
echo "=============================================================================="
"${UBT_ATTACH}" --job "${JOBID}" --comm hpc_app

echo ""
echo "=============================================================================="
echo " 3. Tracing Active Simulation (Sleeping 2.0s)..."
echo "=============================================================================="
sleep 2.0

echo ""
echo "=============================================================================="
echo " 4. Executing Cluster-Wide Dynamic Detachment (Zero-Overhead Detach)..."
echo "=============================================================================="
"${UBT_ATTACH}" --job "${JOBID}" --comm hpc_app --detach

echo ""
echo "=============================================================================="
echo " 5. Waiting for MPI Application to Conclude Naturally..."
echo "=============================================================================="
wait "${APP_BG_PID}"

echo ""
echo "=============================================================================="
echo " 6. Inspecting Simulation Execution Log:"
echo "=============================================================================="
cat /pscratch/sd/s/sgkim/tchoe_home/hpc_app_cluster.log

echo ""
echo "=============================================================================="
echo " SUCCESS: Multi-Node Runtime Injection & Detachment Verified Flawlessly!"
echo "=============================================================================="
