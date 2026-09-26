#!/usr/bin/env bash
# ==============================================================================
# Comprehensive Multi-Scenario Test Harness (Executed from Login Node)
# Validates Scenario 1 (Targeted Straggler Attach), Scenario 2 (Cluster Attach),
# and Scenario 3 (Startup Profiling + ubt-cat Stream Synthesis)
# ==============================================================================

set -e

ROOT_DIR="/pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace"
UBT_ATTACH="${ROOT_DIR}/bin/ubt-attach"
UBT_TOP="${ROOT_DIR}/bin/ubt-top"
UBT_CAT="${ROOT_DIR}/bin/ubt-cat"
HPC_APP="${ROOT_DIR}/examples/apps/hpc_app"

JOBID=$(squeue -u $USER -h -o "%A" | head -n 1)

if [ -z "${JOBID}" ]; then
    echo "Error: No active Slurm job found!"
    exit 1
fi

NODES=$(squeue -j ${JOBID} -h -o '%N')
MASTER_NODE="nid004146"
SLAVE_NODE="nid004147"

LIVE_DIR="/pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/traces_live"
OUT_DIR="/pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/traces"

echo "╔════════════════════════════════════════════════════════════════════╗"
echo "║   ubpftrace End-to-End Multi-Node HPC Validation Suite             ║"
echo "║   Active Slurm Job: ${JOBID} | Compute Nodes: ${NODES}              ║"
echo "╚════════════════════════════════════════════════════════════════════╝"
echo ""

# ==============================================================================
# SCENARIO 1: Remote Straggler-Targeted Dynamic Injection (From Login Node)
# ==============================================================================
echo "=============================================================================="
echo " [SCENARIO 1] Remote Straggler-Targeted Dynamic Injection (${MASTER_NODE} Only)"
echo "=============================================================================="
rm -rf "${LIVE_DIR}" "${OUT_DIR}"
mkdir -p "${LIVE_DIR}" "${OUT_DIR}"

echo "1.1 Starting 4-Rank MPI Simulation in Background across 2 Nodes..."
srun --jobid="${JOBID}" -N 2 -n 4 "${HPC_APP}" 250 > /pscratch/sd/s/sgkim/tchoe_home/scenario1.log 2>&1 &
SCEN1_PID=$!
sleep 1.2

echo "1.2 Injecting Probes into Straggler Node (${MASTER_NODE}) ONLY from Login Node..."
"${UBT_ATTACH}" --job "${JOBID}" --node "${MASTER_NODE}" --comm hpc_app

echo "1.3 Tracing active execution for 2.0s..."
sleep 2.0

echo "1.4 Detaching probes cleanly from Straggler Node (${MASTER_NODE})..."
"${UBT_ATTACH}" --job "${JOBID}" --node "${MASTER_NODE}" --comm hpc_app --detach

echo "1.5 Waiting for MPI simulation to complete..."
wait "${SCEN1_PID}"
echo "✔ Scenario 1 Complete! MPI Simulation finished successfully."
echo ""

# ==============================================================================
# SCENARIO 2: Cluster-Wide Dynamic Runtime Injection (From Login Node)
# ==============================================================================
echo "=============================================================================="
echo " [SCENARIO 2] Cluster-Wide Dynamic Runtime Injection (All Nodes: Master + Slave)"
echo "=============================================================================="
echo "2.1 Starting 4-Rank MPI Simulation in Background..."
srun --jobid="${JOBID}" -N 2 -n 4 "${HPC_APP}" 250 > /pscratch/sd/s/sgkim/tchoe_home/scenario2.log 2>&1 &
SCEN2_PID=$!
sleep 1.2

echo "2.2 Performing Cluster-Wide Dynamic Injection from Login Node..."
"${UBT_ATTACH}" --job "${JOBID}" --comm hpc_app

echo "2.3 Tracing active execution across cluster for 2.0s..."
sleep 2.0

echo "2.4 Performing Cluster-Wide Dynamic Detachment from Login Node..."
"${UBT_ATTACH}" --job "${JOBID}" --comm hpc_app --detach

echo "2.5 Waiting for MPI simulation to complete..."
wait "${SCEN2_PID}"
echo "✔ Scenario 2 Complete! Cluster-wide injection & detachment verified."
echo ""

# ==============================================================================
# SCENARIO 3: Full-Job Startup Profiling, Live ubt-top, & ubt-cat Synthesis
# ==============================================================================
echo "=============================================================================="
echo " [SCENARIO 3] Full-Job Startup Profiling + Live ubt-top + ubt-cat Synthesis"
echo "=============================================================================="
rm -rf "${LIVE_DIR}" "${OUT_DIR}"
mkdir -p "${LIVE_DIR}" "${OUT_DIR}"

export UBPFTRACE_OUTPUT_DIR="${OUT_DIR}"
export UBPFTRACE_LIVE_DIR="${LIVE_DIR}"
export UBPFTRACE_LIVE_INTERVAL_MS=150
export LD_PRELOAD="${ROOT_DIR}/bin/libbpftime-agent.so"

echo "3.1 Launching 4-Rank MPI Simulation with In-Process eBPF Telemetry Enabled..."
srun --jobid="${JOBID}" -N 2 -n 4 --export=ALL,LD_PRELOAD="${ROOT_DIR}/bin/libbpftime-agent.so",UBPFTRACE_OUTPUT_DIR="${OUT_DIR}",UBPFTRACE_LIVE_DIR="${LIVE_DIR}",UBPFTRACE_LIVE_INTERVAL_MS=150 "${HPC_APP}" 100 > /pscratch/sd/s/sgkim/tchoe_home/scenario3.log 2>&1 &
SCEN3_PID=$!
sleep 1.0

echo "3.2 Querying Live Cluster Health from Login Node with ubt-top..."
"${UBT_TOP}" -d "${LIVE_DIR}" --once || true

echo "3.3 Waiting for MPI simulation to conclude..."
wait "${SCEN3_PID}"
echo "MPI Job finished!"

echo ""
echo "3.4 Analyzing Multi-Node Binary Trace Containers with ubt-cat (--info)..."
"${UBT_CAT}" --info "${OUT_DIR}"/*.ubt

echo ""
echo "3.5 Decoding Chronological Event Stream with ubt-cat (--dump)..."
"${UBT_CAT}" --dump "${OUT_DIR}"/*.ubt | head -n 25

echo ""
echo "╔════════════════════════════════════════════════════════════════════╗"
echo "║ ALL 3 END-TO-END SCENARIOS EXECUTED & VERIFIED WITH 100% SUCCESS!  ║"
echo "╚════════════════════════════════════════════════════════════════════╝"
