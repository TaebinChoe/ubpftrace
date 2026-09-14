#!/usr/bin/env bash
set -e

# ==============================================================================
# ubpftrace Automated Build Script for HPC / Perlmutter / Conda Environments
# ==============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${SCRIPT_DIR}"

echo "============================================================"
echo "  UBPFTRACE HPC / PERLMUTTER AUTOMATED BUILD SYSTEM"
echo "============================================================"

# 1. Sanitize include environment variables
unset C_INCLUDE_PATH
unset CPLUS_INCLUDE_PATH

# 2. Determine GCC / G++ compilers (avoid default Cray PE wrapper conflicts)
export CC="${CC:-/usr/bin/gcc}"
export CXX="${CXX:-/usr/bin/g++}"

if ! command -v "${CC}" > /dev/null 2>&1; then
    export CC="$(which gcc)"
fi
if ! command -v "${CXX}" > /dev/null 2>&1; then
    export CXX="$(which g++)"
fi

echo "[1/4] Using C Compiler:   ${CC}"
echo "      Using C++ Compiler: ${CXX}"

# 3. Detect Conda environment
if [ -z "${CONDA_PREFIX}" ]; then
    if [ -d "/pscratch/sd/s/sgkim/tchoe_home/envs/tchoe_env" ]; then
        export CONDA_PREFIX="/pscratch/sd/s/sgkim/tchoe_home/envs/tchoe_env"
        export PATH="${CONDA_PREFIX}/bin:${PATH}"
    fi
fi

if [ -n "${CONDA_PREFIX}" ]; then
    echo "[2/4] Detected Conda Environment: ${CONDA_PREFIX}"
    export LD_LIBRARY_PATH="${CONDA_PREFIX}/lib:${CONDA_PREFIX}/lib64:${LD_LIBRARY_PATH}"
else
    echo "[2/4] No active Conda prefix detected; proceeding with standard system paths."
fi

# 4. Check for libiberty.a (required when linking static libbfd.a)
LIBIBERTY_ARG=""
if [ -n "${CONDA_PREFIX}" ]; then
    if [ -f "${CONDA_PREFIX}/lib/libiberty.a" ]; then
        LIBIBERTY_ARG="-DLIBIBERTY_LIBRARIES=${CONDA_PREFIX}/lib/libiberty.a"
    elif [ -f "${CONDA_PREFIX}/lib64/libiberty.a" ]; then
        LIBIBERTY_ARG="-DLIBIBERTY_LIBRARIES=${CONDA_PREFIX}/lib64/libiberty.a"
    else
        echo "[Notice] libiberty.a not found in Conda prefix. Compiling libiberty with -fPIC..."
        TMP_DIR="${SCRIPT_DIR}/.tmp_libiberty_build"
        rm -rf "${TMP_DIR}"
        git clone --depth 1 https://sourceware.org/git/binutils-gdb.git "${TMP_DIR}"
        (
            cd "${TMP_DIR}/libiberty"
            CC="${CC}" CXX="${CXX}" ./configure --prefix="${CONDA_PREFIX}" --enable-install-libiberty CFLAGS="-fPIC -O2"
            make -j$(nproc)
            cp libiberty.a "${CONDA_PREFIX}/lib/libiberty.a"
        )
        rm -rf "${TMP_DIR}"
        LIBIBERTY_ARG="-DLIBIBERTY_LIBRARIES=${CONDA_PREFIX}/lib/libiberty.a"
        echo "[Notice] Successfully built and installed ${CONDA_PREFIX}/lib/libiberty.a"
    fi
fi

# 5. Configure CMake
echo "[3/4] Configuring CMake build..."
CMAKE_PREFIX_ARGS=""
if [ -n "${CONDA_PREFIX}" ]; then
    CMAKE_PREFIX_ARGS="-DCMAKE_PREFIX_PATH=${CONDA_PREFIX};${CONDA_PREFIX}/lib;${CONDA_PREFIX}/lib64"
fi

cmake -B build -S . \
    -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_TESTS=OFF \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DCMAKE_C_COMPILER="${CC}" \
    -DCMAKE_CXX_COMPILER="${CXX}" \
    ${CMAKE_PREFIX_ARGS} \
    ${LIBIBERTY_ARG}

# 6. Build
echo "[4/4] Building ubpftrace and runtime libraries..."
cmake --build build -j$(nproc)

echo ""
echo "============================================================"
echo "  UBPFTRACE BUILD COMPLETED SUCCESSFULLY!"
echo "============================================================"
echo "Binaries available in ${SCRIPT_DIR}/bin/ :"
ls -lh "${SCRIPT_DIR}/bin"
echo ""
echo "Verify with: ./bin/ubpftrace --version"
