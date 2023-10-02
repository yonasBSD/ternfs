KDIR ?= /lib/modules/$(shell uname -r)/build
LLVM ?= 0

obj-m += eggsfs.o

eggsfs-objs += \
	dir.o \
	err.o \
	export.o \
	file.o \
	inode.o \
	kmod.o \
	metadata.o \
	dentry.o \
	net.o \
	super.o \
	sysctl.o \
	sysfs.o \
	trace.o \
	shuckle.o \
	block.o \
	file.o \
	rs.o \
	crc.o \
	span.o \
	bincode.o \
	revision.o \
	policy.o

EXTRA_CFLAGS = -I$(src) -g -DDEBUG -fdiagnostics-color=always -Wno-declaration-after-statement

export CF = -Wbitwise -Wcontext -Wcast_truncate -Wsparse-all -Wno-shadow -Wnopointer-arith -Wnosizeof-bool -DCONFIG_SPARSE_RCU_POINTER

.PHONY: revision.c
revision.c:
	printf "#include \"sysfs.h\"\nconst char* eggsfs_revision = \"$(shell git describe --always --dirty)\";\n" > revision.c

# C=1 builds with sparse
kmod: revision.c
	make -C $(KDIR) M=$(PWD) C=1 modules

clean:
	make -C $(KDIR) M=$(PWD) clean
	rm -f bincode_tests rs_tests

bincode_tests: bincode_tests.c bincodegen.h bincode.h
	gcc -Wall -g -O2 -fsanitize=undefined,address -fno-sanitize-recover=all bincode_tests.c -o bincode_tests
