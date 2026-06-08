#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <stdarg.h>
#include <sys/resource.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include "kdarshan.h"
#include "kdarshan.skel.h"

static volatile bool exiting = false;

static void sig_handler(int sig) {
    exiting = true;
}

static int libbpf_print_fn(enum libbpf_print_level level, const char *format, va_list args) {
    if (level > LIBBPF_WARN)
        return 0;
    return vfprintf(stderr, format, args);
}

/* Simple Hash Map for path resolution */
#define PATH_MAP_SIZE 1024
struct path_map_entry {
    unsigned long long hash;
    char path[MAX_PATH_LEN];
    int occupied;
} path_map[PATH_MAP_SIZE];

void put_path(unsigned long long hash, const char *p) {
    unsigned int idx = hash % PATH_MAP_SIZE;
    for (int i = 0; i < PATH_MAP_SIZE; i++) {
        unsigned int curr = (idx + i) % PATH_MAP_SIZE;
        if (!path_map[curr].occupied) {
            path_map[curr].hash = hash;
            strncpy(path_map[curr].path, p, MAX_PATH_LEN);
            path_map[curr].occupied = 1;
            return;
        } else if (path_map[curr].hash == hash) {
            strncpy(path_map[curr].path, p, MAX_PATH_LEN);
            return;
        }
    }
}

const char *get_path(unsigned long long hash) {
    unsigned int idx = hash % PATH_MAP_SIZE;
    for (int i = 0; i < PATH_MAP_SIZE; i++) {
        unsigned int curr = (idx + i) % PATH_MAP_SIZE;
        if (!path_map[curr].occupied) {
            return NULL;
        }
        if (path_map[curr].hash == hash) {
            return path_map[curr].path;
        }
    }
    return NULL;
}

/* Ring Buffer Callbacks */
static int handle_path_event(void *ctx, void *data, size_t data_sz) {
    const struct path_event *e = data;
    char resolved[MAX_PATH_LEN];
    
    // Resolve relative path using /proc
    if (e->path[0] == '/') {
        strncpy(resolved, e->path, MAX_PATH_LEN);
    } else {
        char cwd[MAX_PATH_LEN] = "";
        char cwd_sym[128];
        snprintf(cwd_sym, sizeof(cwd_sym), "/proc/%u/cwd", e->pid);
        ssize_t len = readlink(cwd_sym, cwd, sizeof(cwd) - 1);
        if (len > 0) {
            cwd[len] = '\0';
            snprintf(resolved, MAX_PATH_LEN, "%s/%s", cwd, e->path);
        } else {
            strncpy(resolved, e->path, MAX_PATH_LEN);
        }
    }
    
    put_path(e->path_hash, resolved);
    printf("[DISCOVERY] File opened: %s (Hash: %llu, PID: %u, Comm: %s)\n",
           resolved, e->path_hash, e->pid, e->comm);
    return 0;
}

static int handle_dxt_event(void *ctx, void *data, size_t data_sz) {
    const struct dxt_event *e = data;
    const char *path = get_path(e->path_hash);
    char path_buf[MAX_PATH_LEN];
    if (!path) {
        snprintf(path_buf, sizeof(path_buf), "Hash:%llu", e->path_hash);
        path = path_buf;
    }
    
    printf("DXT_TRACE: %s | PID: %u | Offset: %lld | Length: %lld | Start: %.9f | Duration: %.9f | File: %s\n",
           e->write_flag ? "WRITE" : "READ",
           e->pid,
           e->offset,
           e->length,
           (double)e->start_ns / 1e9,
           (double)(e->end_ns - e->start_ns) / 1e9,
           path);
    return 0;
}

/* Counter Name Definitions */
const char *posix_counter_names[] = {
    "POSIX_OPENS",
    "POSIX_FILENOS",
    "POSIX_DUPS",
    "POSIX_READS",
    "POSIX_WRITES",
    "POSIX_SEEKS",
    "POSIX_STATS",
    "POSIX_MMAPS",
    "POSIX_FSYNCS",
    "POSIX_FDSYNCS",
    "POSIX_RENAME_SOURCES",
    "POSIX_RENAME_TARGETS",
    "POSIX_RENAMED_FROM",
    "POSIX_MODE",
    "POSIX_BYTES_READ",
    "POSIX_BYTES_WRITTEN",
    "POSIX_MAX_BYTE_READ",
    "POSIX_MAX_BYTE_WRITTEN",
    "POSIX_CONSEC_READS",
    "POSIX_CONSEC_WRITES",
    "POSIX_SEQ_READS",
    "POSIX_SEQ_WRITES",
    "POSIX_RW_SWITCHES",
    "POSIX_MEM_NOT_ALIGNED",
    "POSIX_MEM_ALIGNMENT",
    "POSIX_FILE_NOT_ALIGNED",
    "POSIX_FILE_ALIGNMENT",
    "POSIX_MAX_READ_TIME_SIZE",
    "POSIX_MAX_WRITE_TIME_SIZE",
    "POSIX_SIZE_READ_0_100",
    "POSIX_SIZE_READ_100_1K",
    "POSIX_SIZE_READ_1K_10K",
    "POSIX_SIZE_READ_10K_100K",
    "POSIX_SIZE_READ_100K_1M",
    "POSIX_SIZE_READ_1M_4M",
    "POSIX_SIZE_READ_4M_10M",
    "POSIX_SIZE_READ_10M_100M",
    "POSIX_SIZE_READ_100M_1G",
    "POSIX_SIZE_READ_1G_PLUS",
    "POSIX_SIZE_WRITE_0_100",
    "POSIX_SIZE_WRITE_100_1K",
    "POSIX_SIZE_WRITE_1K_10K",
    "POSIX_SIZE_WRITE_10K_100K",
    "POSIX_SIZE_WRITE_100K_1M",
    "POSIX_SIZE_WRITE_1M_4M",
    "POSIX_SIZE_WRITE_4M_10M",
    "POSIX_SIZE_WRITE_10M_100M",
    "POSIX_SIZE_WRITE_100M_1G",
    "POSIX_SIZE_WRITE_1G_PLUS",
    "POSIX_STRIDE1_STRIDE",
    "POSIX_STRIDE2_STRIDE",
    "POSIX_STRIDE3_STRIDE",
    "POSIX_STRIDE4_STRIDE",
    "POSIX_STRIDE1_COUNT",
    "POSIX_STRIDE2_COUNT",
    "POSIX_STRIDE3_COUNT",
    "POSIX_STRIDE4_COUNT",
    "POSIX_ACCESS1_ACCESS",
    "POSIX_ACCESS2_ACCESS",
    "POSIX_ACCESS3_ACCESS",
    "POSIX_ACCESS4_ACCESS",
    "POSIX_ACCESS1_COUNT",
    "POSIX_ACCESS2_COUNT",
    "POSIX_ACCESS3_COUNT",
    "POSIX_ACCESS4_COUNT",
    "POSIX_FASTEST_RANK",
    "POSIX_FASTEST_RANK_BYTES",
    "POSIX_SLOWEST_RANK",
    "POSIX_SLOWEST_RANK_BYTES"
};

const char *posix_f_counter_names[] = {
    "POSIX_F_OPEN_START_TIMESTAMP",
    "POSIX_F_READ_START_TIMESTAMP",
    "POSIX_F_WRITE_START_TIMESTAMP",
    "POSIX_F_CLOSE_START_TIMESTAMP",
    "POSIX_F_OPEN_END_TIMESTAMP",
    "POSIX_F_READ_END_TIMESTAMP",
    "POSIX_F_WRITE_END_TIMESTAMP",
    "POSIX_F_CLOSE_END_TIMESTAMP",
    "POSIX_F_READ_TIME",
    "POSIX_F_WRITE_TIME",
    "POSIX_F_META_TIME",
    "POSIX_F_MAX_READ_TIME",
    "POSIX_F_MAX_WRITE_TIME",
    "POSIX_F_FASTEST_RANK_TIME",
    "POSIX_F_SLOWEST_RANK_TIME",
    "POSIX_F_VARIANCE_RANK_TIME",
    "POSIX_F_VARIANCE_RANK_BYTES"
};

int main(int argc, char **argv) {
    struct ring_buffer *rb = NULL;
    struct kdarshan_bpf *skel;
    int err;
    __u32 filter_pid = 0;
    bool dxt_enabled = false;

    // Parse arguments
    int opt;
    while ((opt = getopt(argc, argv, "p:d")) != -1) {
        switch (opt) {
            case 'p':
                filter_pid = atoi(optarg);
                break;
            case 'd':
                dxt_enabled = true;
                break;
            default:
                fprintf(stderr, "Usage: %s [-p target_pid] [-d (enable DXT output)]\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    libbpf_set_print(libbpf_print_fn);

    // Set up signal handlers
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    // Load and verify BPF application
    skel = kdarshan_bpf__open();
    if (!skel) {
        fprintf(stderr, "Failed to open BPF skeleton\n");
        return 1;
    }

    // Set target PID filter
    skel->bss->target_pid = filter_pid;

    err = kdarshan_bpf__load(skel);
    if (err) {
        fprintf(stderr, "Failed to load BPF skeleton\n");
        goto cleanup;
    }

    // Attach tracepoint handlers
    err = kdarshan_bpf__attach(skel);
    if (err) {
        fprintf(stderr, "Failed to attach BPF skeleton\n");
        goto cleanup;
    }

    // Set up ring buffer
    rb = ring_buffer__new(bpf_map__fd(skel->maps.path_events), handle_path_event, NULL, NULL);
    if (!rb) {
        fprintf(stderr, "Failed to create ring buffer\n");
        goto cleanup;
    }

    if (dxt_enabled) {
        err = ring_buffer__add(rb, bpf_map__fd(skel->maps.dxt_events), handle_dxt_event, NULL);
        if (err) {
            fprintf(stderr, "Failed to add DXT map to ring buffer\n");
            goto cleanup;
        }
    }

    printf("kdarshan is running. Tracing %s. Press Ctrl-C to stop...\n", 
           filter_pid ? "target PID" : "all processes");

    // Main ring buffer polling loop
    while (!exiting) {
        ring_buffer__poll(rb, 100);
    }

    // Print final POSIX characterization report
    printf("\n======================== kdarshan POSIX I/O characterization report ========================\n");
    
    unsigned long long next_key, map_key = 0;
    struct kdarshan_file_stats stats;
    int stats_fd = bpf_map__fd(skel->maps.file_stats);

    while (bpf_map_get_next_key(stats_fd, &map_key, &next_key) == 0) {
        err = bpf_map_lookup_elem(stats_fd, &next_key, &stats);
        if (err == 0) {
            const char *filename = get_path(stats.path_hash);
            if (!filename) filename = "UNKNOWN";
            printf("\nFile: %s (Hash: %llu)\n", filename, stats.path_hash);
            printf("--------------------------------------------------------------------------------\n");
            
            // Print integer counters
            for (int i = 0; i < POSIX_NUM_INDICES; i++) {
                if (stats.counters[i] != 0) {
                    printf("  %-30s: %lld\n", posix_counter_names[i], stats.counters[i]);
                }
            }
            // Print float counters
            for (int i = 0; i < POSIX_F_NUM_INDICES; i++) {
                if (stats.fcounters[i] != 0) {
                    if (i == POSIX_F_OPEN_START_TIMESTAMP || i == POSIX_F_OPEN_END_TIMESTAMP ||
                        i == POSIX_F_READ_START_TIMESTAMP || i == POSIX_F_READ_END_TIMESTAMP ||
                        i == POSIX_F_WRITE_START_TIMESTAMP || i == POSIX_F_WRITE_END_TIMESTAMP ||
                        i == POSIX_F_CLOSE_START_TIMESTAMP || i == POSIX_F_CLOSE_END_TIMESTAMP) {
                        printf("  %-30s: %.9f sec (ktime)\n", posix_f_counter_names[i], (double)stats.fcounters[i] / 1e9);
                    } else {
                        printf("  %-30s: %.9f sec\n", posix_f_counter_names[i], (double)stats.fcounters[i] / 1e9);
                    }
                }
            }
        }
        map_key = next_key;
    }
    printf("============================================================================================\n");

cleanup:
    if (rb) ring_buffer__free(rb);
    kdarshan_bpf__destroy(skel);
    return err;
}
