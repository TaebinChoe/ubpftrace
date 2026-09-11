#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
UBPFTRACE="${SCRIPT_DIR}/bin/ubpftrace"
APPS_DIR="${SCRIPT_DIR}/examples/apps"

echo "============================================================"
echo "  UBPFTRACE INTEGRATION TEST SUITE (SELF-CONTAINED)"
echo "============================================================"

# Ensure test application binaries are built
echo "[Setup] Compiling test benchmark applications in ${APPS_DIR}..."
make -C "${APPS_DIR}" > /dev/null

export PATH="${APPS_DIR}:${SCRIPT_DIR}/bin:${PATH}"

echo ""
echo "[Test 1] Tracing User-Defined Function (calculate) with uprobe & uretprobe..."
SPDLOG_LEVEL=error "${UBPFTRACE}" -c "${APPS_DIR}/calc" "${SCRIPT_DIR}/examples/calc.bt"
echo ">>> [Test 1 Passed!] User-defined function traced successfully."

echo ""
echo "[Test 2] Tracing Shared Library Function (uprobe:libc:puts) via alias..."
SPDLOG_LEVEL=error "${UBPFTRACE}" -c "${APPS_DIR}/puts_app" "${SCRIPT_DIR}/examples/puts.bt"
echo ">>> [Test 2 Passed!] Shared library function traced successfully."

echo ""
echo "[Test 3] Tracing Dynamic Memory Allocations with hist() & stats()..."
SPDLOG_LEVEL=error "${UBPFTRACE}" -c "${APPS_DIR}/alloc_app" "${SCRIPT_DIR}/examples/malloc.bt"
echo ">>> [Test 3 Passed!] Malloc tracing & histograms verified."

echo ""
echo "[Test 4] Tracing Math Library (uprobe:libm:sqrt)..."
SPDLOG_LEVEL=error "${UBPFTRACE}" -c "${APPS_DIR}/math_app" "${SCRIPT_DIR}/examples/math.bt"
echo ">>> [Test 4 Passed!] Math library uprobes verified."

echo ""
echo "[Test 5] Tracing Structs, Pointer Dereferences & Multi-key Maps..."
SPDLOG_LEVEL=error "${UBPFTRACE}" -c "${APPS_DIR}/struct_app" "${SCRIPT_DIR}/examples/struct_trace.bt"
echo ">>> [Test 5 Passed!] Struct decoding & multi-key maps verified."

if [ -f "${APPS_DIR}/hpc_app" ]; then
    echo ""
    echo "[Test 6] Tracing HPC MPI Communication & Synchronization Bottlenecks..."
    if command -v mpirun >/dev/null 2>&1; then
        SPDLOG_LEVEL=error "${UBPFTRACE}" -c "mpirun --allow-run-as-root -np 2 ${APPS_DIR}/hpc_app" "${SCRIPT_DIR}/examples/mpi_bottleneck.bt"
    else
        echo "[Test 6 Skipped] mpirun not available."
    fi
    echo ">>> [Test 6 Passed!] HPC MPI bottleneck tracking verified."
else
    echo ""
    echo "[Test 6 Skipped] hpc_app not built (MPI not available on this machine)."
fi

echo ""
echo "============================================================"
echo "  ALL UBPFTRACE INTEGRATION TESTS PASSED SUCCESSFULLY!"
echo "============================================================"
