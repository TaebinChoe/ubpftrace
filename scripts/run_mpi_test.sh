#!/bin/bash
set -e
mkdir -p /pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/traces
export UBPFTRACE_OUTPUT_DIR=/pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/traces
export LD_PRELOAD=/pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/bin/libbpftime-agent.so

echo "=== Running 4-rank MPI Job across 2 nodes with libbpftime-agent.so ==="
srun -n 4 -N 2 ./examples/apps/hpc_app
