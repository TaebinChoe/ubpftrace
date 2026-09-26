#!/usr/bin/env bash
# ==============================================================================
# Complete End-to-End Demo from Master Compute Node (nid004146)
# Tests MPI execution, live ubt-top dashboard, and ubt-cat post-processing
# ==============================================================================

set -e

ROOT_DIR="/pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace"
export PATH="${ROOT_DIR}/bin:${PATH}"

LIVE_DIR="${ROOT_DIR}/traces_live"
OUT_DIR="${ROOT_DIR}/traces"

# Clean up previous trace runs
rm -rf "${LIVE_DIR}" "${OUT_DIR}"
mkdir -p "${LIVE_DIR}" "${OUT_DIR}"

export UBPFTRACE_OUTPUT_DIR="${OUT_DIR}"
export UBPFTRACE_LIVE_DIR="${LIVE_DIR}"
export UBPFTRACE_LIVE_INTERVAL_MS=150
export LD_PRELOAD="${ROOT_DIR}/bin/libbpftime-agent.so"

echo "=============================================================================="
echo " 1. Running 4-Rank MPI Job Across 2 Nodes with Telemetry Active..."
echo "=============================================================================="
srun -N 2 -n 4 "${ROOT_DIR}/examples/apps/hpc_app" 200 > /pscratch/sd/s/sgkim/tchoe_home/hpc_run.log 2>&1 &
APP_PID=$!
echo "MPI Job running in background (PID ${APP_PID})..."

echo ""
echo "=============================================================================="
echo " 2. Querying Live Real-Time Dashboard via ubt-top..."
echo "=============================================================================="
sleep 1.2
echo "--- First Snapshot Query ---"
ubt-top -d "${LIVE_DIR}" --once || true

sleep 1.0
echo ""
echo "--- Second Snapshot Query ---"
ubt-top -d "${LIVE_DIR}" --once || true

# Wait for MPI simulation to finish
wait "${APP_PID}"
echo "Simulation completed successfully!"

echo ""
echo "=============================================================================="
echo " 3. Inspecting Binary Trace Containers with ubt-cat (--info)..."
echo "=============================================================================="
ubt-cat --info "${OUT_DIR}"/*.ubt

echo ""
echo "=============================================================================="
echo " 4. Decoding Multi-Node Event Streams with ubt-cat (--dump)..."
echo "=============================================================================="
ubt-cat --dump "${OUT_DIR}"/*.ubt | head -n 30

echo ""
echo "=============================================================================="
echo " SUCCESS: End-to-End MPI + ubt-top + ubt-cat Demonstration Complete!"
echo "=============================================================================="
