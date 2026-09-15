# Authoritative CLI & Environment Variables Reference

This document provides the complete command-line interface (CLI) and environment variable dictionary for `ubpftrace` and its companion tools.

---

## 1. `ubpftrace` CLI Reference

```text
Usage: ubpftrace [options] filename
       ubpftrace [options] - <stdin input>
       ubpftrace [options] -e 'program'
```

### Core Execution Options

| Option | Argument | Description |
| :--- | :--- | :--- |
| `-e` | `'program'` | Execute BPF program supplied directly as an inline string |
| `-c` | `CMD` | Launch target command `CMD` and automatically attach userspace probes |
| `-p` | `PID` | Attach probes to an existing running process by process ID |
| `-l` | `[REGEX]` | List available traceable functions matching optional regular expression |
| `-v, --verbose` | None | Enable verbose compiler and runtime debug messages |
| `-h, --help` | None | Print command-line usage summary and exit |
| `-V, --version` | None | Display `ubpftrace` version information |
| `-o` | `FILE` | Redirect stdout tracing output to specified file |
| `-f` | `FORMAT` | Output formatting mode (`text`, `json`) |
| `-B` | `MODE` | Output buffering mode (`none`, `line`, `full`) |
| `-q, --quiet` | None | Suppress informational compiler and attachment banners |

---

### Real-Time & Cluster Monitoring Options

| Option | Argument | Description |
| :--- | :--- | :--- |
| `-L, --live` | `SEC` | Enable periodic Scenario A metric snapshotting every `SEC` seconds |
| `--live-ms` | `MS` | Enable sub-second metric snapshotting with millisecond granularity `MS` |
| `--live-dir` | `DIR` | Target directory for out-of-band atomic JSON snapshots (`node_<id>.json`) |
| `--stream` | None | Enable low-latency micro-buffered live stdout event streaming |
| `--stream-flush-ms` | `MS` | Micro-streaming flush timeout in milliseconds (default: `20ms`) |
| `--stream-buffer-kb` | `KB` | Micro-streaming buffer size threshold in KB (default: `8KB`) |

---

### Developer & Diagnostics Options

| Option | Argument | Description |
| :--- | :--- | :--- |
| `--dry-run` | None | Terminate execution immediately after parsing and attaching probes |
| `--verify-llvm-ir` | None | Verify that generated LLVM IR satisfies strict safety invariants |
| `-d, --debug` | `STAGE` | Emit debug AST/bytecode info (`ast`, `types`, `codegen`, `dis`, `all`) |
| `--emit-llvm` | `FILE` | Write unoptimized and optimized LLVM IR to `FILE.original.ll` / `FILE.optimized.ll` |
| `--unsafe` | None | Allow unsafe/destructive operations |

---

## 2. `ubpftrace-cat` CLI Reference

```text
Usage: ubpftrace-cat [OPTIONS] <file1.ubpf / dir> [file2.ubpf ...]
```

| Option | Argument | Description |
| :--- | :--- | :--- |
| `-i, --info, --summary` | None | Display container header metadata, chunk counts, and LZ4 compression stats |
| `-d, --dump` | None | Decode and print chronological event records to stdout (default behavior) |
| `-m, --merge` | None | Merge multiple node container files into a single globally sorted timeline |
| `-c, --chrome` | `FILE.json` | Export merged trace records to Google Chrome / Perfetto JSON format |
| `-o, --output` | `FILE` | Redirect decoded text or merged events to specified file |
| `-h, --help` | None | Display usage help message |

---

## 3. `ubpftrace-top` CLI Reference

```text
Usage: ubpftrace-top [options]
```

| Option | Argument | Description |
| :--- | :--- | :--- |
| `-d, --dir` | `PATH` | Directory containing periodic live JSON snapshots (e.g. `--live-dir`) |
| `-j, --job-id` | `ID` | Slurm Job ID to filter and monitor |
| `-i, --interval` | `SEC` | Dashboard refresh interval in seconds (default: `1.0s`) |
| `--json` | None | Emit cluster aggregation summary as continuous JSON lines to stdout |
| `--once` | None | Perform a single snapshot read, print table, and exit |
| `-h, --help` | None | Display usage help message |

---

## 4. Environment Variables Dictionary

All runtime options can also be configured via environment variables, which is especially convenient in multi-node Slurm job scripts:

| Environment Variable | Equivalent CLI Option | Default Value | Description |
| :--- | :--- | :--- | :--- |
| `UBPFTRACE_OUTPUT_DIR` | `--output` directory | Current Directory | Directory where `.ubpf` containers and `_summary.json` are written |
| `UBPFTRACE_LIVE_DIR` | `--live-dir` | `<out_dir>/.ubpftrace_live_<jobid>` | Directory where periodic JSON snapshots are written |
| `UBPFTRACE_LIVE_INTERVAL_SEC` | `-L <SEC>` | `0` (Disabled) | Snapshot export period in seconds |
| `UBPFTRACE_LIVE_INTERVAL_MS` | `--live-ms <MS>` | `0` (Disabled) | Snapshot export period in milliseconds |
| `UBPFTRACE_STREAM` | `--stream` | `0` (Disabled) | Set to `1` or `true` to enable low-latency micro-buffered streaming |
| `UBPFTRACE_STREAM_FLUSH_MS` | `--stream-flush-ms` | `20` | Streaming timer flush timeout in milliseconds |
| `UBPFTRACE_STREAM_BUFFER_KB` | `--stream-buffer-kb` | `8` | Streaming buffer threshold in kilobytes |
| `BPFTRACE_CACHE_USER_SYMBOLS` | None | `1` | Cache resolved binary symbols in userspace memory for faster lookups |
