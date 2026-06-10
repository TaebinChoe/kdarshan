# kdarshan

`kdarshan` is a transparent, kernel-level eBPF-based I/O characterization tool designed to replicate the output formatting and metrics of the original Darshan tool (including standard POSIX counters and Darshan Extended Tracing (DXT) chronological events).

Unlike userspace-interposition tools, `kdarshan` hooks file I/O operations directly at the kernel syscall level via tracepoints. This means it requires zero library interposition, zero recompilation, and zero application modification.

---

## Directory Structure

All files reside in the root directory:
*   [Makefile](file:///home/bigdatalab/tchoe/kdarshan/Makefile): The build file configures search paths for `libbpf` headers and links compiled programs.
*   [kdarshan.c](file:///home/bigdatalab/tchoe/kdarshan/kdarshan.c): Userspace agent which loads the BPF program, reads configuration, polls the trace ring buffers, and formats final output.
*   [kdarshan.h](file:///home/bigdatalab/tchoe/kdarshan/kdarshan.h): Common headers, stat structs, and index definitions shared between kernel and userspace.
*   [kdarshan.bpf.c](file:///home/bigdatalab/tchoe/kdarshan/kdarshan.bpf.c): BPF C source code loaded into the kernel tracing system calls.
*   [kdarshan_test_suite.c](file:///home/bigdatalab/tchoe/kdarshan/kdarshan_test_suite.c): A multi-threaded C test harness executing read, write, seek, dup, and mmap operations.
*   [verify_test_suite.sh](file:///home/bigdatalab/tchoe/kdarshan/verify_test_suite.sh): Test executor that runs kdarshan over the test suite and runs assertions on output logs.

---

## Complete Quick Start (Copy & Paste)

You can copy, paste, and run the following command blocks in order to fully install, configure, build, and run the project:

### 1. Install Prerequisites
Run this on your Debian/Ubuntu machine to install compilation dependencies and tools:
```bash
sudo apt-get update
sudo apt-get install -y clang llvm libelf-dev zlib1g-dev make gcc pkg-config git
```

### 2. Install BPF tools
Install kernel-specific utilities which include the `bpftool` compiler:
```bash
sudo apt-get install -y linux-tools-common linux-tools-$(uname -r)
```
*(Note: If not globally available, locate `bpftool` on your system. Typical paths include `/usr/lib/linux-tools/...` or `/usr/sbin/bpftool`).*

### 3. Compile and Install `libbpf`
`kdarshan` requires static `libbpf` version 1.0 or higher. Build and compile it locally from source:
```bash
# Clone the library repository in your home directory (e.g. /home/bigdatalab/tchoe)
cd /home/bigdatalab/tchoe
git clone https://github.com/libbpf/libbpf.git
cd libbpf/src
make BUILD_STATIC_ONLY=y DESTDIR=$(pwd)/install install
```

### 4. Build kdarshan
Navigate back to the project root directory and run `make`. 
If your static `libbpf` install folder or `bpftool` path differs, specify them as environment variables during build:
```bash
cd /home/bigdatalab/tchoe/kdarshan

# Clean any stale builds
make clean

# Compile with configured environment variables
make LIBBPF_DIR=/home/bigdatalab/tchoe/libbpf/src/install BPFTOOL=/usr/lib/linux-tools/$(uname -r)/bpftool
```
This produces the `./kdarshan` tracer binary and the `./kdarshan_test_suite` binary.

### 5. Verify Installation
Run the automated test runner to ensure that BPF hooks are correctly logging and aggregating stats:
```bash
./verify_test_suite.sh
```

---

## Configuration Reference

`kdarshan` is configured using configuration files and environment variables.

### Environment Variables
*   `DARSHAN_CONFIG_PATH`: Absolute path to the configuration file (e.g. `/home/bigdatalab/tchoe/kdarshan/test_darshan.conf`).
*   `DARSHAN_OUTPUT_MODE`: Overrides the configuration file's output mode (`performance` or `security`).

### Configuration File Options
Configuration parameters are specified using `KEY VALUE` pairs (lines starting with `#` are ignored as comments).

1.  `MOD_ENABLE`
    *   **Value**: `DXT_POSIX`
    *   **Description**: Enables Darshan Extended Tracing (DXT) POSIX module tracing. If this is enabled, `kdarshan` records chronologically ordered read and write actions in userspace ring buffers. If omitted, DXT tracing is disabled, and only cumulative metrics are tracked.
2.  `OUTPUT_MODE`
    *   **Values**: `performance` (default) or `security`
    *   **Description**:
        *   `performance`: Focuses on minimizing tracer overhead. Captured events are stored silently in-memory. Output is printed to stdout only when `kdarshan` receives `SIGINT` (Ctrl-C) or terminates.
        *   `security`: Focuses on real-time security auditing (similar to the `-P` option in `eauditd`). Prints event logs immediately as they happen to standard output, while still appending the complete final Darshan log upon tracer exit.

#### Example Configuration (`test_darshan.conf`)
```text
# Enable DXT trace logging
MOD_ENABLE DXT_POSIX

# Enable security-first real-time printing
OUTPUT_MODE security
```

---

## Detailed Output Behaviors

`kdarshan` produces output based on both the selected `OUTPUT_MODE` and whether DXT is enabled (`MOD_ENABLE DXT_POSIX` or the `-d` CLI option).

### Buffering & Real-time Latency
To match the strict logging guarantees of `eaudit`:
1.  **Zero Kernel-Space Buffering**: Unlike `eaudit` which caches up to 100 events in a per-CPU buffer in the kernel before flushing to the ring buffer, `kdarshan` submits every single trace event **immediately** (`bpf_ringbuf_submit`). This removes all kernel-level buffering delays.
2.  **Unbuffered Output**: At startup, `kdarshan` disables standard library buffering for both `stdout` and `stderr` using `setvbuf(..., _IONBF)`. Every path discovery or DXT event printed is written directly to the underlying output file descriptor immediately without userspace memory buffering.

### What happens when Security Mode is enabled BUT DXT is disabled?
If you configure `OUTPUT_MODE security` but **do not** enable DXT tracing, the following behavior occurs:
1.  **Real-Time Path Discovery**: Every time a tracked process opens a file, `kdarshan` will print a path discovery notification to standard output in real time:
    ```text
    [DISCOVERY] File opened: /home/user/kdarshan/testfile.bin (Hash: 36028943068081356, PID: 1234, Comm: my_app)
    ```
2.  **No Real-Time I/O Logs**: Because DXT tracing is disabled, read and write operations are not monitored via ring buffers. No `X_POSIX: pid=...` read/write events will be printed in real time.
3.  **Final Summary Log**: When you stop the tracer via `Ctrl-C`, it prints the standard final summary log containing only the aggregated **POSIX module counters** (e.g. `POSIX_OPENS`, `POSIX_READS`, `POSIX_WRITES`, etc.). No DXT tables will be printed.

### Execution Summary Matrix

| OUTPUT_MODE | DXT Enabled? | Real-time output behavior | Final output behavior |
| :--- | :--- | :--- | :--- |
| `performance` | **No** | None (Silent during run) | Prints only POSIX module counter tables. |
| `performance` | **Yes** | None (Silent during run) | Prints POSIX module counters + chronological DXT tables. |
| `security` | **No** | Prints `[DISCOVERY]` events immediately. | Prints POSIX module counter tables. |
| `security` | **Yes** | Prints `[DISCOVERY]` & DXT I/O read/write traces immediately. | Prints POSIX module counters + chronological DXT tables. |

---

## Running the Profiler

The tracer binary must be run with root privileges:
```bash
# Run with a config file
sudo DARSHAN_CONFIG_PATH=/home/bigdatalab/tchoe/kdarshan/test_darshan.conf ./kdarshan

# Run filtering for a specific target process in performance mode
sudo ./kdarshan -p <PID>

# Run filtering for a specific process with DXT enabled
sudo ./kdarshan -p <PID> -d
```
