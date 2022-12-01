KDIR ?= /data/home/fmazzol/src/eggsfs/kmod/linux-5.4.237/
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
	span.o

EXTRA_CFLAGS = -I$(src) -g -DDEBUG -fdiagnostics-color=always -Wno-declaration-after-statement

export CF = -Wbitwise -Wcontext -Wcast_truncate -Wsparse-all -Wno-shadow -Wnosizeof-void -DCONFIG_SPARSE_RCU_POINTER

# C=1 builds with sparse
kmod:
	make -C $(KDIR) M=$(PWD) C=1 modules

clean:
	make -C $(KDIR) M=$(PWD) clean
	rm -f bincode_tests

bincode_tests: bincode_tests.c bincodegen.h bincode.h
	clang -Wall -g -O2 -fsanitize=undefined,address,integer,function -fno-sanitize-recover=all bincode_tests.c -o bincode_tests

writefile: writefile.c
	gcc -Wall -g -O2 writefile.c -o writefile