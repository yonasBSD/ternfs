# pyeggsfs

Pyeggsfs is the python prototype of eggsfs. See the [proposal](https://xtxmarketscom.sharepoint.com/:w:/s/ECN/EdVNBAzB7klPsVw6CxkfAvwB0LGu4pbtf-Gafr0tMnWNKw?e=2LaGl8) for more info on eggsfs.

## Usage

Pyeggsfs has 4 daemon processes which are required for core operation. To get started, you should run at least one of each. When they are running, you can poke the running database using the `basic_client.py` script.

Additional daemons for things like garbage collection will be added later, for now the prototype just creates a bunch of garbage which can be carefully removed with custom commands.

Note that currently everything is configured to use `localhost`, so you need to run all the daemons and basic client on the same box. There is no technical reason for this other than it's more convenient for testing (and sidesteps discovery/DNS).

### raftless_metadata.py

This script runs a single metadata shard. As currently configured, you need to run two shards: shard 0 (home of new dirs) and shard 1 (home of root). Like so:

```
$ ./raftless_metadata.py 0 /path/to/persist/folder/
$ ./raftless_metadata.py 1 /path/to/persist/folder/
```

The reason new directories are created exclusively on shard 0 is `cross_dir_coord.MkDir.initial`:

```python
    @state_func
    def initial(self, c: 'CrossDirCoordinator') -> Optional[str]:
        # Create a new directory object in a randomly selected shard
        shard = 0#random.randint(0, 255)
        # ...
```
Replace the `0` with `random.randint(0, 255)` (or whatever) to activate more shards.

### cross_dir_coord.py

Runs the cross-directory coordinator. There can only be one.

```
$ ./cross_dir_coord.py /path/to/persist/folder/
```

### block_service.py

Runs a single block service process. These can be scaled arbitrarily. The storage class can be any u8 greater than or equal to 2 (0 and 1 are reserved for blockless storage)

```
$ ./block_service.py /path/to/block/folder/ --storage_class STORAGE_CLASS
```

### shuckle.py

Runs shuckle (formerly known as block service coordinator).

```
$ ./shuckle.py
```

With shuckle running, you can navigate to http://localhost:5000 to see a view of all block services.

## Using the "Basic Client"

`basic_client.py` is a basic userspace client for the filesystem. Each invocation runs a single command and exits. It waits for 2 seconds before returning a "timed-out" error (which can be misleading for cross-dir requests which, if accepted, will be perpetually retried by cross_dir_coord). Also note that only absolute paths are supported, there is no concept of a working directory.

Below are some common commands. See `./basic_client.py --help` for a full list of commands, some were created just to test certain functions which would otherwise be expected to be performed by a (currently non-existant) garbage collector daemon.

### mkdir

Creates a new subdirectory. Cross-directory coordinator must be available.

```
$ ./basic_client.py mkdir /a
CrossDirResponse(request_id=1662392431, status_code=<ResponseStatus.OK: 0>, new_inode=3, text='')
```

### rmdir

Removes a subdirectory. Target must be empty (i.e. no living entries). Note that this is a "soft-delete", i.e. the directory is still linked into the parent's dead map after the operation. Cross-directory coordinator must be available.

```
$ ./basic_client.py rmdir /a
CrossDirResponse(request_id=1662392423, status_code=<ResponseStatus.OK: 0>, new_inode=None, text='')
```

### stat

Stats an inode. The resolve is subtly different for directories and non-directories:

```
$ ./basic_client.py stat /b
inode=0x0000000000000002
type=DIRECTORY
mtime=84557619643473241
parent=0x0020000000000000
opaque=b''
```

```
$ ./basic_client.py stat /hello.txt
inode=0x2020000000000003
type=FILE
mtime=84558235023282121
size=22
```

### mv

Moves an inode. See design doc for rules on what can be moved and overwritten.

```
$ ./basic_client.py mv /a /b/a
CrossDirResponse(request_id=1662394419, status_code=<ResponseStatus.OK: 0>, new_inode=3, text='')
```

### ls

Lists a directory.

```
$ ./basic_client.py ls /b
['a']
```

A plain ls will pull results from a directory's living map. You can also query the dead map with ls_dead_{le,ge} providing a timestamp (defaults to 0), e.g.

```
$ ./basic_client.py ls /c
['d']

$ ./basic_client.py rmdir /c/d
CrossDirResponse(request_id=1662394677, status_code=<ResponseStatus.OK: 0>, new_inode=None, text='')

$ ./basic_client.py ls /c
[]

$ ./basic_client.py ls_dead_ge /c 84557827787438800
['d']
```

### create_test_file

This command creates a file with contents `b'hello, world\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00'` split across three spans using different storage modes. Very useful for testing. I'm open to suggestions as to how this can be improved though. Under the hood it's doing quite a lot, see `basic_client.do_create_test_file` for details.

```
$ ./basic_client.py create_test_file /hello.txt
LinkEdenFileResp()
```

### cat

Copies a file's contents to stdout. Only works if all the non-parity blocks of the file can be located (reconstruction not implemented).

```
$ ./basic_client.py cat /hello.txt
hello, world
```

### rm

Deletes files. Similar to rmdir, this is a soft-delete.

```
$ ./basic_client.py rm /hello.txt
DeleteFileResp()
```
