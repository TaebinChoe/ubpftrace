#!/bin/bash
set -e
mkdir -p /pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/traces
export UBPFTRACE_OUTPUT_DIR=/pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/traces

echo "=== Running 2-node 4-rank Simultaneous Tracing with ubpftrace ==="
srun -N 2 -n 4 ./bin/ubpftrace -c "./examples/apps/hpc_app" -e '
  uprobe:./examples/apps/hpc_app:simulate_computation {
      printf("GlobalRank %d on Node %d started iter %d\n", rank, node, arg1);
  }
'
