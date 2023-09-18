# EggsFS

A distributed file system.

## Goals

The target use case for EggsFS is the kind of machine learning we do at XTX: reading and writing large immutable files. With "immutable" we mean files that do not need modifying after they are first created. With "large" we mean that most of the storage space will be taken up by files bigger than a few MBs.

We don't expect new directories to be created often, and files (or directories) to be moved between directories often. In terms of numbers, we expect the upper bound for EggsFS to roughly be the upper bound for the data we're planning for a single data center:

- 10EB of logical file storage (i.e. if you sum all file sizes = 10EB)
- 1 trillion files -- average ~10MB file size
- 10 billion directories -- average ~100 files per directory
- 100,000 clients

We want to drive the filesystem with commodity hardware and Ethernet networking.

We want the system to be robust in various ways:

* Witnessing half-written files should be impossible -- a file is fully written by the writer or not readable by other clients (TODO reference link)
* Power loss or similar failure of storage or metadata nodes should not result in a corrupted filesystems (be it metadata or filesystem corruption)
* Corrupted reads due to hard drives bitrot should be exceedingly unlikely
  * TODO reference to CRC32C strategy for spans/blocks
  * TODO some talking about RocksDB's CRCs (<https://rocksdb.org/blog/2022/07/18/per-key-value-checksum.html>)
* Data loss should be exceedingly unlikely, unless we suffer a datacenter-wide catastrophic event (fire, flooding, datacenter-wide vibration, etc)
  * TODO link to precise storage strategy to make this more precise
  * TODO some talking about future multi data center plans
* The filesystem should keep working through maintenance or failure of metadata or storage nodes

Moreover, we want the filesystem to be usable as a "normal" filesystem (although _not_ a POSIX compliant filesystem) as opposed to a blob storage with some higher level API a-la AWS S3. This is mostly due to the cost we would face if we had to upgrade all the current users of the compute cluster to a non-file API.

Finally, we want to be able to restore deleted files or directories, using a configurable "permanent deletion" policy.

## Components

![components (1)](https://REDACTED)

TODO decorate list below with links drilling down on specific concepts.

* **servers**
  * **shuckle**
    * 1 logical instance
    * `eggsshuckle`, Go binary
    * state currently persisted through SQLite (1 physical instance), should move to a Galera cluster soon (see #41)
    * TCP -- both bincode and HTTP
    * stores metadata about a specific EggsFS deployment
      * shard/cdc addresses
      * block services addressea and storage statistics
      * latency histograms
    * serves web UI (e.g. <http://REDACTED>)
  * **filesystem data**
    * **metadata**
      * **shard**
        * 256 logical instances
        * `eggsshard`, C++ binary
        * stores all metadata for the filesystem
          * file attributes (size, mtime, atime)
          * directory attributes (mtime)
          * directories listings (includes file/directory names)
          * file to blocks mapping
          * block service to file mapping
        * UDP bincode req/resp
        * state persisted through RocksDB, currently single physical instance per logical instance, will move to a Paxos (or similar) cluster per logical instance (see #56)
        * communicates with shuckle to fetch block services, register itself, insert statistics
    * **CDC**
      * 1 logical instance
      * `eggscdc`, C++ binary
      * coordinates actions which span multiple directories
        * create directory
        * remove directory
        * move file or directory between from one directory to the other
        * "Cross Directory Coordinator"
      * UDP bincode req/resp
      * very little state required
        * information about which transactions are currently being executed and which are queued (currently transactions are executed serially)
        * directory -> parent directory mapping to perform "no loops" checks
      * state persisted through RocksDB, currently single physical instance, will move to Paxos or similar just like shards (see #56)
      * communicates with the shards to perform the cross-directory actions
      * communicates with shuckle to register itself, fetch shards, insert statistics
  * **block service**
    * up to 1 million logical instances
    * 1 logical instance = 1 disk
    * 1 physical instance handles ~100 logical instances (since there are ~100 disks per server)
    * `eggsblocks`, Go binary (for now, will be rewritten in C++ eventually)
    * stores "blocks", which are blobs of data which contain file contents
    * each file is split in many blocks stored on many block services (so that if up to 4 block services fail we can always recover files)
    * single instances are not redundant, the redundancy is handled by spreading files over many instances so that we can recover their contents
    * TCP bincode req/resp
    * extremely dumb, the only state is the blobs themselves
    * its entire job is efficiently streaming blobs of data from disks into TCP connections
    * communicates with shuckle to register itself and to update information about free space, number of blocks, etc.
* **clients**, these all talk to all of the servers
  * **cli**
    * `eggscli`, Go binary
    * toolkit to perform various tasks, most notably
      * migrating contents of dead block services (`eggscli migrate`)
      * moving around blocks so that files are stored efficiently (`eggscli defrag`, currently WIP, see #50)
  * **kmod**
    * `eggsfs.ko`, C Linux kernel module
    * kernel module implementing `mount -t eggsfs ...`
    * the most fun and pleasant part of the codebase
  * **FUSE**
    * `eggsfuse`, Go FUSE implementation of an eggsfs client
    * much slower but should be almost fully functional (there are some limitations concerning when a file gets flushed)
* **daemons**, these also talk to all of the servers
  * **GC**
    * `eggsgc`, Go binary
    * permanently deletes expired snapshots (i.e. deleted but not yet purged data)
    * cleans up all blocks for permanently deleted files
  * **scrubber**
    * TODO see #32
    * goes around detecting and repairing bitrot
* **additional tools**
  * `eggsrun`, a tool to quickly spin up an EggsFS instance
  * `eggstests`, runs some integration tests

## Building

```
% ./build.sh alpine
```

Will build all the artifacts apart from the Kernel module. The output will be in `build/alpine`. Things will be built in an Alpine Linux container, so that everything will be fully statically linked. `./build.sh release` will build outside the container, everything will still be linked statically apart from glibc, which cannot be reliably linked statically.

If you want to build outside the Alpine container (e.g. if you wan to do `go run` as I suggest below), you need to have the right Go in your PATH. On ETD dev boxes this can be accomplished by adding `/opt/go1.18.4/bin` to your path.

## Testing

CI is currently very long (~40 mins for the "quick" tests, ~3h for the full tests). Therefore you might have to wait a while for it.

If you have a small fix and want to push to `main`, run at least

```
% go run . -filter 'mounted' -short -build-type sanitized
```

In `go/eggstests`. This takes around 3 minutes, and will surface gross mistakes. Note that this does _not_ test the kernel module (CI does).

TODO easy way to run kmod tests locally (just requires a bit of reworking of [ci.sh](kmod/ci.sh), plus pushing Ubuntu base image to artifactory).

## Playing with a local EggsFS instance

```
% cd go/eggsrun
% go run . -data-dir <data-dir>
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

## VS Code

Most of the codebase is understandable by VS Code/LSP:

* Code in `go/` just works out of the box with the [Go extension](https://code.visualstudio.com/docs/languages/go). I (fmazzol) open a separate VS Code window which specifically has the `eggsfs/go` directory open, since the Go extension doesn't seem to like working from a subdirectory.
* Code in `cpp/`:
    - Disable existing C++ integrations for VS Code (I don't remember which exact C++ extension caused me trouble -- something by Microsoft itself).
    - Install the [clangd extension](https://marketplace.visualstudio.com/items?itemName=llvm-vs-code-extensions.vscode-clangd).
    - Generate `compile_commands.json` with
        
        ```
        % cd cpp
        % ./build.py debug
        % cp ./build/debug/compile_commands.json .
        ```
        
        If you change the build process (i.e. if you change CMake files) you'll need to generate and copy `compile_commands.json` again.
* Code in `kmod/`:
    - Build the module (see instructions earlier on in this document).
    - Then generate `compile_command.json` with `./kmod/gen_compile_commands.sh`.
