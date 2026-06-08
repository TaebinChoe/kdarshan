CLANG ?= clang
CFLAGS ?= -g -O2 -Wall
BPFTOOL ?= /usr/lib/linux-hwe-6.8-tools-6.8.0-100/bpftool
LIBBPF_DIR = /home/bigdatalab/tchoe/libbpf/install

all: kdarshan kdarshan_test_suite

kdarshan.bpf.o: kdarshan.bpf.c kdarshan.h vmlinux.h
	$(CLANG) -g -O2 -target bpf -I$(LIBBPF_DIR)/usr/include -c kdarshan.bpf.c -o kdarshan.bpf.o

kdarshan.skel.h: kdarshan.bpf.o
	$(BPFTOOL) gen skeleton kdarshan.bpf.o > kdarshan.skel.h

kdarshan: kdarshan.c kdarshan.skel.h kdarshan.h
	$(CC) $(CFLAGS) -I$(LIBBPF_DIR)/usr/include kdarshan.c $(LIBBPF_DIR)/usr/lib64/libbpf.a -lelf -lz -o kdarshan

kdarshan_test_suite: kdarshan_test_suite.c
	$(CC) $(CFLAGS) kdarshan_test_suite.c -o kdarshan_test_suite

clean:
	rm -f kdarshan kdarshan.bpf.o kdarshan.skel.h kdarshan_test_suite test_suite.bin

.PHONY: all clean
