#!/usr/bin/env bash
set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$DIR"

echo "================================================================="
echo "  MRE TEST: Dynamic Injection -> Detachment -> Re-Attachment"
echo "================================================================="

echo "=== Step 1: Clean Environment ==="
pkill -9 -f mre_target || true
rm -rf /dev/shm/bpftime* /dev/shm/*.json /dev/shm/ubpf* "$DIR/traces"/.ubpftrace_live_* "$DIR/traces"/*.json /tmp/mre_*.json /tmp/mre_target.log

echo "=== Step 2: Compile & Launch mre_target ==="
gcc -g -O0 -fno-inline "$DIR/tests/mre_target.c" -o "$DIR/tests/mre_target"
"$DIR/tests/mre_target" > /tmp/mre_target.log 2>&1 &
TARGET_PID=$!
echo "Target process started with PID: $TARGET_PID"
sleep 0.3

echo ""
echo "=== Step 3: Cycle 1 — Initial Dynamic Injection ==="
"$DIR/bin/ubt-attach" -p "$TARGET_PID" -s "$DIR/tests/mre_probe.bt"
sleep 1.2

echo ">>> Cycle 1 Validation (Checking Telemetry & Maps):"
ls -la "$DIR/traces/.ubpftrace_live_$TARGET_PID" 2>/dev/null || ls -la "$DIR/traces"/.ubpftrace_live_* 2>/dev/null || true
python3 -c "
import json, glob
files = glob.glob('$DIR/traces/.ubpftrace_live_*/node_*.json')
if not files:
    print('ERROR: No live snapshot JSON found!')
    exit(1)
with open(files[0]) as f:
    d = json.load(f)
maps = d.get('maps', {})
print(f'Cycle 1 Live Maps count: {len(maps)}')
for mname, mval in maps.items():
    print(f'  [Map {mname}]: entries = {mval.get(\"entries\", {})}')
assert len(maps) > 0, 'Cycle 1 maps must not be empty!'
"

echo ""
echo "=== Step 4: Detachment ==="
"$DIR/bin/ubt-attach" -p "$TARGET_PID" -d
sleep 0.5

echo ">>> Verifying process is still running after detachment:"
if kill -0 "$TARGET_PID" 2>/dev/null; then
    echo "✔ Target process PID $TARGET_PID is alive and running smoothly!"
else
    echo "FAILED: Target process crashed during detachment!"
    exit 1
fi

echo ""
echo "=== Step 5: Cycle 2 — Re-Attachment ==="
"$DIR/bin/ubt-attach" -p "$TARGET_PID" -s "$DIR/tests/mre_probe.bt"
sleep 1.2

echo ">>> Cycle 2 Validation (Checking Telemetry & Maps upon Re-Attach):"
ls -la "$DIR/traces/.ubpftrace_live_$TARGET_PID" 2>/dev/null || ls -la "$DIR/traces"/.ubpftrace_live_* 2>/dev/null || true
python3 -c "
import json, glob
files = glob.glob('$DIR/traces/.ubpftrace_live_*/node_*.json')
if not files:
    print('ERROR: No live snapshot JSON found!')
    exit(1)
with open(files[0]) as f:
    d = json.load(f)
maps = d.get('maps', {})
print(f'Cycle 2 Live Maps count: {len(maps)}')
for mname, mval in maps.items():
    print(f'  [Map {mname}]: entries = {mval.get(\"entries\", {})}')
assert len(maps) > 0, 'Cycle 2 maps must NOT be empty upon re-attach!'
"

echo ""
echo "=== Step 6: Multi-Cycle Stress Test (3 Additional Cycles) ==="
for i in {3..5}; do
    echo "--- Cycle $i: Detach & Re-Attach ---"
    "$DIR/bin/ubt-attach" -p "$TARGET_PID" -d >/dev/null 2>&1
    sleep 0.3
    "$DIR/bin/ubt-attach" -p "$TARGET_PID" -s "$DIR/tests/mre_probe.bt" >/dev/null 2>&1
    sleep 0.8
    python3 -c "
import json, glob
files = glob.glob('$DIR/traces/.ubpftrace_live_*/node_*.json')
with open(files[0]) as f:
    d = json.load(f)
maps = d.get('maps', {})
assert len(maps) > 0, 'Cycle $i maps must NOT be empty!'
print('  Cycle $i passed: ' + ', '.join(f'{k}={v.get(\"entries\",{})}' for k, v in maps.items()))
"
done

echo ""
echo "=== Step 7: Teardown & Cleanup ==="
kill -9 "$TARGET_PID" || true
rm -rf /dev/shm/bpftime* /dev/shm/*.json /dev/shm/ubpf* "$DIR/traces"/.ubpftrace_live_* "$DIR/traces"/*.json /tmp/mre_*.json /tmp/mre_target.log
echo "================================================================="
echo "  MRE VERIFICATION RESULT: ALL CYCLES PASSED (100% SUCCESS)"
echo "================================================================="
