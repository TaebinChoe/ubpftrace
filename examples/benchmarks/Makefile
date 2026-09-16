CC := /usr/bin/gcc
CFLAGS ?= -O2 -g -fno-omit-frame-pointer

TARGETS = bench_micro_probe bench_lustre_io bench_omp bench_cuda bench_nccl bench_mpi_p2p bench_mpi_collectives

all: $(TARGETS)

bench_micro_probe: bench_micro_probe.c
	$(CC) $(CFLAGS) -fno-inline -o $@ $<

bench_lustre_io: bench_lustre_io.c
	$(CC) $(CFLAGS) -o $@ $<

bench_omp: bench_omp.c
	$(CC) $(CFLAGS) -fopenmp -o $@ $<

bench_cuda: bench_cuda.c
	$(CC) $(CFLAGS) -L/opt/nvidia/hpc_sdk/Linux_x86_64/23.1/cuda/12.0/targets/x86_64-linux/lib -lcudart -Wl,-rpath,/opt/nvidia/hpc_sdk/Linux_x86_64/23.1/cuda/12.0/targets/x86_64-linux/lib -o $@ $< 2>/dev/null || true

bench_nccl: bench_nccl.c
	$(CC) $(CFLAGS) -L/pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/examples/apps -lnccl -Wl,-rpath,/pscratch/sd/s/sgkim/tchoe_home/FGCS/ubpftrace/examples/apps -o $@ $< 2>/dev/null || true

bench_mpi_p2p: bench_mpi_p2p.c
	$(CC) $(CFLAGS) -I/opt/cray/pe/mpich/8.1.28/ofi/gnu/12.3/include -L/opt/cray/pe/lib64 -L/opt/cray/libfabric/1.22.0/lib64 -lmpi_gnu -lfabric -Wl,-rpath,/opt/cray/pe/lib64 -Wl,-rpath,/opt/cray/libfabric/1.22.0/lib64 -o $@ $< 2>/dev/null || true

bench_mpi_collectives: bench_mpi_collectives.c
	$(CC) $(CFLAGS) -I/opt/cray/pe/mpich/8.1.28/ofi/gnu/12.3/include -L/opt/cray/pe/lib64 -L/opt/cray/libfabric/1.22.0/lib64 -lmpi_gnu -lfabric -Wl,-rpath,/opt/cray/pe/lib64 -Wl,-rpath,/opt/cray/libfabric/1.22.0/lib64 -o $@ $< 2>/dev/null || true

clean:
	rm -f $(TARGETS)
