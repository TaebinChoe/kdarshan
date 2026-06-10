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
*   **Original Darshan log format alignment**:
    *   **Standard POSIX Mode (Without `-d`)**: Outputs the general header, mount point list, and tab-separated POSIX counters exactly replicating original `darshan-parser` output.
    *   **DXT Mode (With `-d`)**: Buffers events in userspace and outputs grouped, space-aligned chronological trace records for each file on exit, matching the original `darshan-dxt-parser` format.
*   **File System Mount & FS Type Resolution**: Parses `/proc/mounts` and performs longest-prefix path matching to map each tracked file path to its correct mount point and filesystem type (e.g. `/` and `ext4` or `/pscratch` and `lustre`).
*   **Process Metadata Caching**: Gathers the command line and UID of target processes immediately at startup to ensure headers are correctly populated even if the process exits before `kdarshan` is stopped.

---

## Directory Structure

*   [kdarshan.h](kdarshan.h): Common headers, structs, and enum offsets.
*   [kdarshan.bpf.c](kdarshan.bpf.c): BPF tracepoint hooks (kernel space).
*   [kdarshan.c](kdarshan.c): BPF skeleton loader, path resolution, and report formatter (userspace).
*   [Makefile](Makefile): Build configuration pointing to local static libbpf libraries.
*   [kdarshan_test_suite.c](kdarshan_test_suite.c): Test suite designed for multi-metric coverage.
*   [verify_test_suite.sh](verify_test_suite.sh): Test harness running assertions on BPF outputs.

---

## Installation & Setup

### 1. Install Prerequisites
You will need tools to compile C programs, compile BPF bytecode, and link libraries.

On Debian/Ubuntu:
```bash
sudo apt-get update
sudo apt-get install -y clang llvm libelf-dev zlib1g-dev make gcc pkg-config
```

### 2. Install `bpftool`
`bpftool` is required to generate the skeleton header from the compiled BPF object.
```bash
# Install kernel-specific tools containing bpftool
sudo apt-get install -y linux-tools-common linux-tools-$(uname -r)
```
*Note: If the package is not found or you are using a custom/mainline kernel, locate `bpftool` on your system. It is usually found under `/usr/lib/linux-tools-...` or `/usr/sbin/bpftool`.*

### 3. Build & Install `libbpf`
`kdarshan` requires `libbpf` v1.0 or higher. If your system's package manager only provides an older version (e.g., Ubuntu 22.04 defaults to v0.5.0, which cannot parse modern 64-bit enum BTF structures), compile and install it from source:

```bash
git clone https://github.com/libbpf/libbpf.git
cd libbpf/src
make BUILD_STATIC_ONLY=y DESTDIR=$(pwd)/install install
```
Take note of the absolute path to your `libbpf/src/install` directory.

---

## Configuration

### 1. Build Configuration
Before building, you must configure the path to your `libbpf` installation and `bpftool` in the `Makefile`.

1.  Open the [Makefile](Makefile).
2.  Set the following variables:
    *   `LIBBPF_DIR`: Point this to your compiled static `libbpf` install directory (e.g. `/home/user/libbpf/src/install`).
    *   `BPFTOOL`: Point this to the absolute path of `bpftool` if it is not in your global `PATH` (e.g. `/usr/lib/linux-tools/6.8.0-100-generic/bpftool`).

Alternatively, you can pass these variables directly on the command line during compilation:
```bash
make LIBBPF_DIR=/path/to/libbpf/install BPFTOOL=/path/to/bpftool
```

### 2. Runtime Configuration (Modes & Module Activation)
`kdarshan` supports configuration via a configuration file and environment variables.

#### Environment Variables
*   `DARSHAN_CONFIG_PATH`: Path to the `darshan.conf` configuration file.
*   `DARSHAN_OUTPUT_MODE`: Directly sets the output mode, overriding the configuration file (`performance` or `security`).

#### Configuration File Format
You can specify settings in a configuration file (e.g., `test_darshan.conf`). The parser supports:
*   `#` for comments
*   `MOD_ENABLE DXT_POSIX`: Enables the DXT profiling module. This is equivalent to running `kdarshan` with the `-d` command-line option.
*   `OUTPUT_MODE <mode>`: Sets the operational and output behavior. Valid options are:
    *   `performance` (default): Focus on logging performance. Logs are buffered in-memory and written only when the tracer terminates.
    *   `security`: Focus on security/auditing. Prints real-time path discovery events and DXT I/O events (reads/writes) immediately to standard output, while still generating the standard Darshan final log summary upon exit.

Example configuration file content:
```text
# Enable DXT Posix module
MOD_ENABLE DXT_POSIX

# Enable security-first real-time auditing mode
OUTPUT_MODE security
```

---

## Build Instructions

Compile the BPF object, skeleton headers, userspace loader, and the test suite:
```bash
make
```

Clean up all generated compilation files:
```bash
make clean
```

---

## Running the Profiler

Since `kdarshan` loads programs into the kernel, it must be run with root privileges (using `sudo`).

```text
Usage: sudo [DARSHAN_CONFIG_PATH=...] [DARSHAN_OUTPUT_MODE=...] ./kdarshan [-p target_pid] [-d]
```

### Command Options:
*   `-p <target_pid>`: Profiles only the process with the given PID. If not specified, `kdarshan` tracks system-wide process accesses.
*   `-d`: Enables DXT (Darshan Extended Tracing) mode. Logs trace events into memory and prints a grouped DXT report on exit (equivalent to `MOD_ENABLE DXT_POSIX` in config).

### Output Modes Detail:

#### 1. Performance-First Mode (Default)
Optimized for minimum overhead.
*   **Behavior**: Events are captured silently in the kernel and userspace maps/buffers.
*   **Output**: No output is printed during execution. When the tracer receives `SIGINT` (Ctrl-C) or `SIGTERM`, it prints the complete Darshan log report.
*   **Log Composition**: If DXT is enabled (`-d` or `MOD_ENABLE DXT_POSIX`), the report will contain **both** the standard POSIX counter tables and the detailed chronological DXT trace tables.

#### 2. Security-First Mode (`OUTPUT_MODE security`)
Optimized for real-time security auditing (similar to the `-P` option in `eauditd`).
*   **Behavior**:
    *   Whenever a file is opened, `kdarshan` prints a `[DISCOVERY]` event in real-time to standard output.
    *   If DXT is enabled, each read and write event is formatted and printed in real-time to standard output.
*   **Real-time Event Format**:
    *   **Path Discovery**: `[DISCOVERY] File opened: <path> (Hash: <hash>, PID: <pid>, Comm: <comm>)`
    *   **DXT I/O Event**: `X_POSIX: pid=<pid> <read|write>(file="<path>", offset=<offset>, length=<length>) start=<start_sec> end=<end_sec>`
*   **Termination Behavior**: Upon receiving `SIGINT` (Ctrl-C), it still prints the complete final summary Darshan log report to standard output.

---

## Checking & Interpreting the Results

### 1. Standard POSIX Output (Without `-d`)
Standard output matches original `darshan-parser` output and consists of three main sections:
*   **Header Comments**: Metadata about the job including command line (`# exe:`), UID (`# uid:`), start/end timestamps, and execution run time.
*   **Mount Points**: All active file system mounts on the host (`# mount entry: mount_pt fs_type`).
*   **Counter Entries**: Tab-separated entries for every file tracked, structured as:
    `<module>\t<rank>\t<record id>\t<counter>\t<value>\t<file name>\t<mount pt>\t<fs type>`
    
    *Example:*
    ```text
    POSIX	-1	36028943068081356	POSIX_OPENS	1	/home/user/kdarshan/testfile.bin	/	ext4
    POSIX	-1	36028943068081356	POSIX_READS	50	/home/user/kdarshan/testfile.bin	/	ext4
    POSIX	-1	36028943068081356	POSIX_BYTES_READ	3200	/home/user/kdarshan/testfile.bin	/	ext4
    POSIX	-1	36028943068081356	POSIX_F_READ_TIME	0.000069	/home/user/kdarshan/testfile.bin	/	ext4
    ```

### 2. DXT POSIX Output (With `-d`)
DXT output matches original `darshan-dxt-parser` output and shows chronological I/O transaction tables grouped per file:
*   **DXT File Info**: Lists the file hash (`file_id`), hostname, total write/read counts, and file system details.
*   **DXT Columns Header**:
    `# Module    Rank  Wt/Rd  Segment          Offset       Length    Start(s)      End(s)   [OST]`
*   **DXT Event Entries**:
    - `Module`: Always `X_POSIX`.
    - `Rank`: Always `0` (or MPI rank if integrated).
    - `Wt/Rd`: `read` or `write`.
    - `Segment`: Sequence number of the operation for this file.
    - `Offset`: Target offset in the file.
    - `Length`: Number of bytes requested.
    - `Start(s)` / `End(s)`: Start and end timestamp in seconds, relative to the tracing startup time.
    - `[OST]`: Lustre OST target index (`-1` for non-Lustre filesystems).

    *Example:*
    ```text
    # DXT, file_id: 36028943068081356, file_name: /home/user/kdarshan/testfile.bin
    # DXT, rank: 0, hostname: user-desktop
    # DXT, write_count: 50, read_count: 50
    # DXT, mnt_pt: /, fs_type: ext4
    # Module    Rank  Wt/Rd  Segment          Offset       Length    Start(s)      End(s)   [OST]
     X_POSIX        0   write         0                0               64       4.9076       4.9077    [-1]
     X_POSIX        0    read         1                0               64       4.9079       4.9079    [-1]
     X_POSIX        0   write         2               64               64       4.9080       4.9080    [-1]
    ```

---

## Validation & Testing

Run the automated validation check suite to verify BPF metrics:
```bash
./verify_test_suite.sh
```
This runs the deterministic `kdarshan_test_suite` program under standard and DXT tracing, and asserts all 16 metrics and DXT counts.
