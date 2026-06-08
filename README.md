# kdarshan

`kdarshan` is a transparent, kernel-level eBPF reimplementation of the Darshan HPC I/O characterization tool, including Darshan Extended Tracing (DXT) capabilities.

Unlike the original Darshan which operates as a userspace interposer library requiring dynamic linking or binary interposition, `kdarshan` runs transparently at the kernel level using Compile Once Run Everywhere (CO-RE) eBPF hooks. It requires no modifications, relinking, or restarts of the target HPC applications.

---

## Features

*   **Zero-Interposition Transparency**: Hooks relevant POSIX-style system calls at the tracepoint level.
*   **Comprehensive Counter Metrics**:
    *   **Operation Counts**: Reads, writes, seeks, opens, stats, dups, mmaps, fsyncs, and fdatasyncs.
    *   **I/O Volume**: Total bytes read and written, max read/write offsets.
    *   **Aesthetic Alignment Tracking**: Detects memory buffer alignment and file block alignment.
    *   **Spatial Patterns**: Consecutive and sequential access tracking.
    *   **Switches**: Tracks read-write mode switches.
    *   **Size Bucketing**: Automatically categorizes reads and writes into size-range counters.
*   **DXT Tracing**: Captures nanosecond-accurate start/end timestamps, offsets, and durations for individual read/write operations.
*   **Unified Event Pipeline**: Merges path discovery and trace events through a single unified ring buffer to guarantee ordered resolution and print correct paths in live outputs.

---

## Prerequisites

1.  **OS & Kernel**: Linux kernel 6.0+ is recommended (fully tested on kernel `6.8` with BTF enabled).
2.  **Dependencies**:
    *   `clang` (v12+ for BPF bytecode compilation)
    *   `gcc` (for userspace linking)
    *   `bpftool` (for generating the skeleton header from the BPF object)
    *   `libelf` and `zlib` (required for libbpf)
3.  **Local Libbpf**: The project compiles against a local static build of `libbpf` v1.3.0 (included in the build tree) to guarantee compatibility with `BTF_KIND_ENUM64` metadata layout.

---

## Directory Structure

*   [kdarshan.h](file:///home/bigdatalab/tchoe/kdarshan/kdarshan.h): Common headers, structs, and enum offsets.
*   [kdarshan.bpf.c](file:///home/bigdatalab/tchoe/kdarshan/kdarshan.bpf.c): BPF tracepoint hooks (kernel space).
*   [kdarshan.c](file:///home/bigdatalab/tchoe/kdarshan/kdarshan.c): BPF skeleton loader, path resolution, and report formatter (userspace).
*   [Makefile](file:///home/bigdatalab/tchoe/kdarshan/Makefile): Build configuration pointing to local static libbpf libraries.
*   [kdarshan_test_suite.c](file:///home/bigdatalab/tchoe/kdarshan/kdarshan_test_suite.c): Test suite designed for multi-metric coverage.
*   [verify_test_suite.sh](file:///home/bigdatalab/tchoe/kdarshan/verify_test_suite.sh): Test harness running assertions on BPF outputs.

---

## Build Instructions

To build the userspace loader, BPF object, skeleton headers, and the test suite:

```bash
make
```

To clean up all generated files:

```bash
make clean
```

---

## Usage

Since `kdarshan` loads eBPF programs, it must be run with root privileges (using `sudo`).

```text
Usage: sudo ./kdarshan [-p target_pid] [-d]
```

### Options
*   `-p <target_pid>`: Profiles only the process with the given PID. If not specified, `kdarshan` profiles all system-wide processes.
*   `-d`: Enables live DXT (Darshan Extended Tracing) trace logs, printing individual read/write operations to stdout as they occur.

### Examples

1.  **System-Wide Tracing**:
    Trace all I/O operations occurring across the entire system.
    ```bash
    sudo ./kdarshan
    ```

2.  **Profile a Specific Application with DXT**:
    Trace a target application (e.g. PID `12345`) and print real-time transaction timings and absolute file paths:
    ```bash
    sudo ./kdarshan -p 12345 -d
    ```

3.  **Generating a Characterization Report**:
    Run `kdarshan` (optionally filtering by PID). Let the target run, and then press `Ctrl-C` (SIGINT) to terminate tracing. `kdarshan` will print the aggregated POSIX job metrics to stdout:
    ```bash
    sudo ./kdarshan -p 12345 > my_job_profile.log
    # Let target run and finish
    # Press Ctrl-C to trigger writeout
    ```

---

## Validation & Testing

An automated verification suite is included to assert the mathematical correctness of all tracked metrics:

```bash
./verify_test_suite.sh
```

This script:
1.  Launches `kdarshan_test_suite` (which executes a pre-planned set of operations, including unaligned access, sequential write sequences, fstat, dup, fsync, mmap, and R/W switches).
2.  Launches `kdarshan` attached to that PID.
3.  Parses the output log and validates every single counter value against the expected values.
4.  Reports `ALL TESTS PASSED SUCCESSFULLY!` if all checks are correct.
