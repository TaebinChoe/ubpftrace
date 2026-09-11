CC ?= gcc
MPICC ?= mpicc
CFLAGS ?= -O2 -g -fno-omit-frame-pointer

TARGETS = calc puts_app alloc_app math_app struct_app hpc_app

all: $(TARGETS)

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

hpc_app: hpc_app.c
	@which $(MPICC) > /dev/null 2>&1 && $(MPICC) $(CFLAGS) -o $@ $< || echo "Warning: mpicc not found, skipping hpc_app build"

clean:
	rm -f $(TARGETS)
