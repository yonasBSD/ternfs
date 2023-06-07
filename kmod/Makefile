KDIR ?= /lib/modules/$(shell uname -r)/build
LLVM ?= 0

obj-m += eggsfs.o

eggsfs-objs += \
	dir.o \
	err.o \
	file.o \
	inode.o \
	kmod.o \
	metadata.o \
	namei.o \
	net.o \
	skb.o \
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
	bincode.o

EXTRA_CFLAGS = -I$(src) -g -DDEBUG -fdiagnostics-color=always -Wno-declaration-after-statement

export CF = -Wbitwise -Wcontext -Wcast_truncate -Wsparse-all -Wno-shadow -Wnosizeof-void -DCONFIG_SPARSE_RCU_POINTER

# C=1 builds with sparse
kmod:
	make -C $(KDIR) M=$(PWD) C=1 modules

clean:
	make -C $(KDIR) M=$(PWD) clean
	rm -f bincode_tests rs_tests

bincode_tests: bincode_tests.c bincodegen.h bincode.h
	gcc -Wall -g -O2 -fsanitize=undefined,address -fno-sanitize-recover=all bincode_tests.c -o bincode_tests
