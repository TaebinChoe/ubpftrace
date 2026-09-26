#!/bin/bash
# ==============================================================================
# test_dynamic_attach_e2e.sh - End-to-End Multi-Node Dynamic Tracing Demonstration
# ==============================================================================
set -euo pipefail

ROOT_DIR="/pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace"
TRACE_DIR="${ROOT_DIR}/traces"
mkdir -p "${TRACE_DIR}"

# 1. Discover Active Slurm Job
if [ -z "${SLURM_JOB_ID:-}" ]; then
    SLURM_JOB_ID=$(squeue -u "$USER" -h -o "%A" | head -n 1)
fi

if [ -z "${SLURM_JOB_ID}" ]; then
    echo "Error: No active Slurm job found. Please allocate nodes first (e.g., salloc -N 2 ...)"
    exit 1
fi

echo "================================================================================"
echo " UBPFTRACE: Multi-Node Dynamic Runtime Injection & Analysis E2E Test"
echo "================================================================================"
echo " Workspace Root   : ${ROOT_DIR}"
echo " Trace Directory  : ${TRACE_DIR}"
echo " Slurm Job ID     : ${SLURM_JOB_ID}"
echo "--------------------------------------------------------------------------------"

# Clean up previous instances and traces on compute nodes and login node
pkill -9 -f "hpc_app" 2>/dev/null || true
pkill -9 -f "srun.*hpc_app" 2>/dev/null || true
srun --jobid="${SLURM_JOB_ID}" --overlap -N 2 pkill -9 -f hpc_app 2>/dev/null || true
sleep 1
rm -rf "${TRACE_DIR}"/* "${TRACE_DIR}"/.ubpftrace_live_*

# Step 1: Launch Unmodified MPI Workload in Background (200 iterations ~ 5s)
echo -e "\n[STEP 1] Launching Unmodified Multi-Node MPI Application on Compute Nodes..."
srun --jobid="${SLURM_JOB_ID}" -N 2 --ntasks-per-node=2 \
  "${ROOT_DIR}/examples/apps/hpc_app" 200 \
  > "${TRACE_DIR}/hpc_app.log" 2>&1 &
APP_PID=$!
echo "  Application launched in background (Runner PID: ${APP_PID})"
sleep 1

# Step 2: Dynamic Runtime Injection via .bt Script from Login Node
echo -e "\n[STEP 2] Dynamically Injecting 'trace_hpc.bt' into Running Application from Login Node..."
"${ROOT_DIR}/bin/ubt-attach" \
  -j "${SLURM_JOB_ID}" \
  --comm hpc_app \
  -s "${ROOT_DIR}/examples/apps/trace_hpc.bt"

# Step 3: Real-Time Cluster Aggregation Snapshot
echo -e "\n[STEP 3] Inspecting Real-Time Cluster Aggregation Dashboard (Single Cycle)..."
sleep 1
"${ROOT_DIR}/bin/ubt-top" -j "${SLURM_JOB_ID}" --once

# Wait for application to finish and finalize container index & trailers
echo -e "\n  Waiting for application run to complete and flush .ubt containers..."
wait "${APP_PID}" 2>/dev/null || true
sleep 1

# Step 4: Inspect 2MB Stripe-Aligned Binary Trace Containers
echo -e "\n[STEP 4] Inspecting Binary Trace Container Metadata & LZ4 Compression..."
"${ROOT_DIR}/bin/ubt-cat" --info "${TRACE_DIR}"/ubpftrace_"${SLURM_JOB_ID}"_node_*.ubt

# Step 5: Chronologically Merge & Decode Multi-Node Event Streams
echo -e "\n[STEP 5] Decoding & Merging Chronological Trace Stream (Top 30 Records)..."
"${ROOT_DIR}/bin/ubt-cat" --merge "${TRACE_DIR}"/ubpftrace_"${SLURM_JOB_ID}"_node_*.ubt | head -n 30

# Step 6: Export Multi-Node Timeline to Perfetto/Chrome Tracing
echo -e "\n[STEP 6] Exporting Multi-Node Timeline to Chrome Tracing JSON..."
"${ROOT_DIR}/bin/ubt-cat" \
  --chrome "${TRACE_DIR}/timeline.json" \
  "${TRACE_DIR}"/ubpftrace_"${SLURM_JOB_ID}"_node_*.ubt

ls -lh "${TRACE_DIR}/timeline.json"

echo -e "\n================================================================================"
echo " ✔ SUCCESS: End-to-End Dynamic Tracing & Analysis Complete!"
echo "================================================================================"
