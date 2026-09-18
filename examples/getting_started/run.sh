#!/usr/bin/env bash
# ==============================================================================
# ubpftrace Getting Started: Automated Test & Execution Script
# ==============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"

UBPFTRACE="${ROOT_DIR}/bin/ubpftrace"
UBPFTRACE_TOP="${ROOT_DIR}/bin/ubpftrace-top"
UBPFTRACE_CAT="${ROOT_DIR}/bin/ubpftrace-cat"
APP="${SCRIPT_DIR}/target_app"

echo "=============================================================================="
echo " 1. Compiling Target Application..."
echo "=============================================================================="
make -C "${SCRIPT_DIR}"

echo ""
echo "=============================================================================="
echo " [SCENARIO 1] Function Tracing & Arguments (01_function_tracing.bt)"
echo " Command: ${UBPFTRACE} -c '${APP} 5' ${SCRIPT_DIR}/01_function_tracing.bt"
echo "=============================================================================="
"${UBPFTRACE}" -c "${APP} 5" "${SCRIPT_DIR}/01_function_tracing.bt"

echo ""
echo "=============================================================================="
echo " [SCENARIO 2] In-Memory Map Aggregations (02_map_aggregation.bt)"
echo " Command: ${UBPFTRACE} -c '${APP} 20' ${SCRIPT_DIR}/02_map_aggregation.bt"
echo "=============================================================================="
"${UBPFTRACE}" -c "${APP} 20" "${SCRIPT_DIR}/02_map_aggregation.bt"

echo ""
echo "=============================================================================="
echo " [SCENARIO 3] Periodic Windowed Aggregation (03_windowed_metrics.bt)"
echo " Command: ${UBPFTRACE} --no-warnings -c '${APP} 25' ${SCRIPT_DIR}/03_windowed_metrics.bt"
echo "=============================================================================="
"${UBPFTRACE}" --no-warnings -c "${APP} 25" "${SCRIPT_DIR}/03_windowed_metrics.bt"

echo ""
echo "=============================================================================="
echo " [SCENARIO 4] Live Dashboard with ubpftrace-top (04_live_top_dashboard.bt)"
echo "=============================================================================="
LIVE_DIR="${SCRIPT_DIR}/.live_demo"
rm -rf "${LIVE_DIR}"

UBPFTRACE_LIVE_DIR="${LIVE_DIR}" "${UBPFTRACE}" --live-ms 300 -c "${APP} 20" "${SCRIPT_DIR}/04_live_top_dashboard.bt" &
BG_PID=$!

sleep 1.0
echo "--- Real-time Cluster Query via ubpftrace-top ---"
"${UBPFTRACE_TOP}" -d "${LIVE_DIR}" --once || true

wait "${BG_PID}"

echo ""
echo "=============================================================================="
echo " [SCENARIO 5] Trace Container Recording & Decoding with ubpftrace-cat"
echo "=============================================================================="
rm -f "${SCRIPT_DIR}"/*.ubpf
UBPFTRACE_OUTPUT_DIR="${SCRIPT_DIR}" "${UBPFTRACE}" -c "${APP} 10" "${SCRIPT_DIR}/05_trace_container_cat.bt"

echo "--- Container Info (ubpftrace-cat --info) ---"
"${UBPFTRACE_CAT}" --info "${SCRIPT_DIR}"/*.ubpf

echo "--- Decoded Events (ubpftrace-cat --dump) ---"
"${UBPFTRACE_CAT}" --dump "${SCRIPT_DIR}"/*.ubpf

echo ""
echo "=============================================================================="
echo " All Getting Started Scenarios Executed Successfully!"
echo "=============================================================================="
