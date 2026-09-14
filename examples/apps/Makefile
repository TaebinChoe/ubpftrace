ifeq ($(CC),cc)
  CC := /usr/bin/gcc
endif
CC ?= /usr/bin/gcc
MPICC ?= $(shell which mpicc 2>/dev/null || which cc 2>/dev/null)
CFLAGS ?= -O2 -g -fno-omit-frame-pointer

STANDARD_TARGETS = calc puts_app alloc_app math_app struct_app lustre_io_app omp_app

all: $(STANDARD_TARGETS) hpc_app mpi_p2p_app cuda_sync_app nccl_collective_app

calc: calc.c
	$(CC) $(CFLAGS) -fno-inline -o $@ $<

puts_app: puts_app.c
	$(CC) $(CFLAGS) -o $@ $<

alloc_app: alloc_app.c
	$(CC) $(CFLAGS) -o $@ $<

math_app: math_app.c
	$(CC) $(CFLAGS) -fno-builtin -o $@ $< -lm

struct_app: struct_app.c
	$(CC) $(CFLAGS) -fno-inline -o $@ $<

lustre_io_app: lustre_io_app.c
	$(CC) $(CFLAGS) -o $@ $<

omp_app: omp_app.c
	$(CC) $(CFLAGS) -fopenmp -o $@ $<

cuda_sync_app: cuda_sync_app.c
	$(CC) $(CFLAGS) -L/opt/nvidia/hpc_sdk/Linux_x86_64/23.1/cuda/12.0/targets/x86_64-linux/lib -lcudart -Wl,-rpath,/opt/nvidia/hpc_sdk/Linux_x86_64/23.1/cuda/12.0/targets/x86_64-linux/lib -o $@ $< 2>/dev/null || true

libnccl.so: libnccl_mock.c
	$(CC) $(CFLAGS) -shared -fPIC -o $@ $< 2>/dev/null || true

nccl_collective_app: nccl_collective_app.c libnccl.so
	$(CC) $(CFLAGS) -L. -lnccl -Wl,-rpath,. -o $@ $< 2>/dev/null || true

mpi_p2p_app: mpi_p2p_app.c
	$(CC) $(CFLAGS) -I/opt/cray/pe/mpich/8.1.28/ofi/gnu/12.3/include -L/opt/cray/pe/lib64 -L/opt/cray/libfabric/1.22.0/lib64 -lmpi_gnu -lfabric -Wl,-rpath,/opt/cray/pe/lib64 -Wl,-rpath,/opt/cray/libfabric/1.22.0/lib64 -o $@ $< 2>/dev/null || true

hpc_app: hpc_app.c
	@if [ -n "$(MPICC)" ] && $(MPICC) $(CFLAGS) -o $@ $< 2>/dev/null; then \
		echo "Built hpc_app successfully with $(MPICC)"; \
	elif [ -n "$(MPICC)" ] && $(MPICC) $(CFLAGS) -o $@ $< -L/opt/cray/libfabric/1.22.0/lib64 -Wl,-rpath,/opt/cray/libfabric/1.22.0/lib64 -lfabric 2>/dev/null; then \
		echo "Built hpc_app successfully with Cray MPICH & Libfabric"; \
	else \
		rm -f $@; \
		echo "Notice: MPI compiler not found or libraries not linked; skipping hpc_app build."; \
	fi

clean:
	rm -f $(STANDARD_TARGETS) hpc_app mpi_p2p_app cuda_sync_app nccl_collective_app libnccl.so
