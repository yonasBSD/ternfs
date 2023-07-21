# EggsFS

A distributed file system. No high level description yet apart from a somewhat outdated [proposal](https://xtxmarketscom.sharepoint.com/:w:/s/ECN/EdVNBAzB7klPsVw6CxkfAvwB0LGu4pbtf-Gafr0tMnWNKw?e=2LaGl8). If you have questions you're probably better off asking @fmazzol.

EggsFS is a collection of binaries starting with `eggs`, plus a kernel module, `eggsfs.ko`. Specifically:

* `eggsshard` and `eggscdc`, are the processes storing the metadata (what directories and files are on the filesystem);
* `eggsblocks` reads and writes blobs of data (blocks), of which files are made of;
* `eggsshuckle` acts a nameserver of sorts storing where the shards, CDC, and block services are;
* `eggsgc` continuously scans the filesystem for garbage that can be collected;
* `eggscli` is a command line tool to interact with the filesystem directly, and also perform some maintenance tasks (e.g. migrating the contents of a block service);
* `eggsfuse` is an EggsFS client implemented with fuse;
* `eggsfs.ko` is an EggsFS client implemented as a kernel module;
* `eggsrun` is a tool to quickly spin up an EggsFS instance;
* `eggstests` runs some integration tests.

`eggsshard` and `eggscdc` are written in C++. `eggsfs.ko` is written in C (duh). Everything else is written in Go.

## Building

```
% ./build.sh alpine
```

Will build all the artifacts apart from the Kernel module. The output will be in `build/alpine`. Things will be built in an Alpine Linux container, so that everything will be fully statically linked. `./build.sh release` will build outside the container, everything will still be linked statically apart from glibc, which cannot be reliably linked statically.

## Playing with a local EggsFS instance

```
% cd go/eggsrun
% go run . -dir <data-dir>
```
The above will run all the processes needed to run EggsFS. This includes:

* 256 metadata shards;
* 1 cross directory coordinator (CDC)
* A bunch of block services (this is tunable with the `-flash-block-services`, `-hdd-block-services`, and `-failure-domains` flags)
* 1 shuckle instance

A multitude of directories to persist the whole thing will appear in `<data-dir>`. The filesystem will also be mounted using FUSE under `<data-dir>/fuse/mnt`.

## Building the Kernel module

```
% cd kmod
% ./fetchlinux.sh # fetch linux sources
% (cd linux && make oldconfig && make prepare && make -j) # build Linux
% make KDIR=linux -j kmod
```

## Playing with the Kernel module

There's infrastructure to spin up a minimal QEMU image with `eggsfs.ko` inserted, see `kmod/restartsession.sh`.
