#!/usr/bin/env bash
set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$DIR"

echo "=== 1. Cleaning SHM and Traces ==="
pkill -9 -f mre_target || true
rm -rf /dev/shm/bpftime* /dev/shm/*.json /dev/shm/ubpf* traces/.ubpftrace_live_* traces/*.ubpt traces/mre_target.log

echo "=== 2. Starting mre_target process ==="
export UBPFTRACE_OUTPUT_DIR="$DIR/traces"
export UBPFTRACE_LIVE_DIR="$DIR/traces/.ubpftrace_live_local"
mkdir -p "$DIR/traces/.ubpftrace_live_local"

./tests/mre_target > "$DIR/traces/mre_target.log" 2>&1 &
TARGET_PID=$!
echo "mre_target PID: $TARGET_PID"
sleep 0.5

echo "=== 3. Cycle 1: Initial Attach ==="
./bin/ubt-attach -p "$TARGET_PID" -s tests/mre_probe.bt
sleep 1.5

echo "=== Cycle 1 Snapshot Check ==="
ls -la "$DIR/traces/.ubpftrace_live_local" || true
cat "$DIR/traces/.ubpftrace_live_local"/node_*.json 2>/dev/null || echo "No JSON snapshot found!"

echo "=== 4. Cycle 1: Detach ==="
./bin/ubt-attach -p "$TARGET_PID" -d
sleep 0.5

echo "=== Process Check after Detach ==="
if kill -0 "$TARGET_PID" 2>/dev/null; then
    echo "mre_target is still running smoothly!"
else
    echo "ERROR: mre_target crashed on detach!"
    exit 1
fi

echo "=== 5. Cycle 2: Re-Attach ==="
./bin/ubt-attach -p "$TARGET_PID" -s tests/mre_probe.bt
sleep 1.5

echo "=== Cycle 2 Snapshot Check ==="
ls -la "$DIR/traces/.ubpftrace_live_local" || true
cat "$DIR/traces/.ubpftrace_live_local"/node_*.json 2>/dev/null || echo "No JSON snapshot found!"

echo "=== 6. Cleanup ==="
pkill -9 -f mre_target || true
echo "=== Done ==="
