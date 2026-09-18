CC = /usr/bin/gcc
CFLAGS ?= -O2 -g -fno-inline -fno-omit-frame-pointer

all: target_app

target_app: target_app.c
	$(CC) $(CFLAGS) -o target_app target_app.c

clean:
	rm -f target_app *.ubpf *.json
	rm -rf .ubpftrace_live*

.PHONY: all clean
