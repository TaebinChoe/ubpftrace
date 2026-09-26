#!/usr/bin/env bash
set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"

echo "========================================================================="
echo "  STANDALONE MRE: Pure Frida Gum Dynamic Attach -> Detach -> Re-Attach"
echo "========================================================================="

echo "=== 1. Compile minimal target ==="
gcc -O0 -g -rdynamic -fno-inline "$DIR/target.c" -o "$DIR/target"

echo "=== 2. Compile minimal agent.so ==="
gcc -shared -fPIC -O2 -g \
    "$DIR/agent.c" \
    -I"$ROOT/build/bpftime/FridaGum-prefix/src/FridaGum" \
    "$ROOT/build/bpftime/FridaGum-prefix/src/FridaGum/libfrida-gum.a" \
    -lpthread -lm -ldl \
    -o "$DIR/libmre_agent.so"

echo "=== 2b. Compile minimal injector ==="
gcc -O2 -g \
    "$DIR/injector.c" \
    -I"$ROOT/build/bpftime/FridaCore-prefix/src/FridaCore" \
    "$ROOT/build/bpftime/FridaCore-prefix/src/FridaCore/libfrida-core.a" \
    -lpthread -lm -ldl -lresolv \
    -o "$DIR/injector"

echo "=== 3. Launch target process ==="
pkill -9 -f "$DIR/target" 2>/dev/null || true
"$DIR/target" > /tmp/mre_standalone_target.log 2>&1 &
TARGET_PID=$!
sleep 0.3

echo "Target PID is $TARGET_PID"

echo "=== 4. Dynamic Injection into PID $TARGET_PID ==="
"$DIR/injector" "$TARGET_PID" "$DIR/libmre_agent.so" mre_agent_init

sleep 0.5

# Python IPC helper function
send_ipc() {
    python3 -c "
import socket, sys
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
addr = b'\0mre-agent-$TARGET_PID'
try:
    s.connect(addr)
    s.sendall(b'$1\n')
    resp = s.recv(1024).decode('utf-8').strip()
    print(resp)
except Exception as e:
    print(f'ERROR: {e}', file=sys.stderr)
    sys.exit(1)
finally:
    s.close()
"
}

echo ""
echo "=== 5. Cycle 1: Verify Initial Attach & Interception ==="
STATUS1=$(send_ipc "status")
echo "Initial Status: $STATUS1"
sleep 0.5
STATUS2=$(send_ipc "status")
echo "Status after 0.5s: $STATUS2"

COUNT1=$(echo "$STATUS1" | grep -o 'count=[0-9]*' | cut -d= -f2)
COUNT2=$(echo "$STATUS2" | grep -o 'count=[0-9]*' | cut -d= -f2)
echo "Cycle 1 count delta: $((COUNT2 - COUNT1)) (expected ~25 hits in 0.5s)"

if [ "$COUNT2" -gt "$COUNT1" ]; then
    echo "✔ Cycle 1 PASSED: Function calls intercepted successfully!"
else
    echo "✖ Cycle 1 FAILED: Count did not increase!"
    kill -9 "$TARGET_PID"
    exit 1
fi

echo ""
echo "=== 6. Detachment Verification ==="
DETACH_RESP=$(send_ipc "detach")
echo "Detach response: $DETACH_RESP"
sleep 0.5
STATUS_DETACHED_1=$(send_ipc "status")
sleep 0.5
STATUS_DETACHED_2=$(send_ipc "status")

COUNT_D1=$(echo "$STATUS_DETACHED_1" | grep -o 'count=[0-9]*' | cut -d= -f2)
COUNT_D2=$(echo "$STATUS_DETACHED_2" | grep -o 'count=[0-9]*' | cut -d= -f2)
echo "Detached counts: D1=$COUNT_D1, D2=$COUNT_D2"

if [ "$COUNT_D1" -eq "$COUNT_D2" ]; then
    echo "✔ Detachment PASSED: Hook removed cleanly, 0 overhead, count unchanged!"
else
    echo "✖ Detachment FAILED: Hook is still firing after detach!"
    kill -9 "$TARGET_PID"
    exit 1
fi

echo ""
echo "=== 7. Cycle 2: Re-Attachment Verification ==="
REATTACH_RESP=$(send_ipc "reattach")
echo "Reattach response: $REATTACH_RESP"
sleep 0.5
STATUS_REATTACHED_1=$(send_ipc "status")
sleep 0.5
STATUS_REATTACHED_2=$(send_ipc "status")

COUNT_R1=$(echo "$STATUS_REATTACHED_1" | grep -o 'count=[0-9]*' | cut -d= -f2)
COUNT_R2=$(echo "$STATUS_REATTACHED_2" | grep -o 'count=[0-9]*' | cut -d= -f2)
echo "Reattached counts: R1=$COUNT_R1, R2=$COUNT_R2 (delta: $((COUNT_R2 - COUNT_R1)))"

if [ "$COUNT_R2" -gt "$COUNT_R1" ]; then
    echo "✔ Cycle 2 PASSED: Re-attachment succeeded and count resumes incrementing!"
else
    echo "✖ Cycle 2 FAILED: Re-attachment failed to intercept!"
    kill -9 "$TARGET_PID"
    exit 1
fi

echo ""
echo "=== 8. Stress Test: 5 Rapid Detach/Re-Attach Cycles ==="
for i in {3..7}; do
    echo "--- Cycle $i ---"
    send_ipc "detach" >/dev/null
    sleep 0.1
    send_ipc "reattach" >/dev/null
    sleep 0.3
    C1=$(send_ipc "status" | grep -o 'count=[0-9]*' | cut -d= -f2)
    sleep 0.3
    C2=$(send_ipc "status" | grep -o 'count=[0-9]*' | cut -d= -f2)
    echo "  Cycle $i: delta=$((C2 - C1)) hits"
    if [ "$C2" -le "$C1" ]; then
        echo "✖ Cycle $i failed!"
        kill -9 "$TARGET_PID"
        exit 1
    fi
done

echo ""
echo "=== 9. Cleanup ==="
kill -9 "$TARGET_PID" 2>/dev/null || true
echo "========================================================================="
echo "  STANDALONE MRE RESULT: 100% SUCCESS (ATTACH -> DETACH -> REATTACH)"
echo "========================================================================="
