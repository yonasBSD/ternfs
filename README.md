# EggsFS

See the [proposal](https://xtxmarketscom.sharepoint.com/:w:/s/ECN/EdVNBAzB7klPsVw6CxkfAvwB0LGu4pbtf-Gafr0tMnWNKw?e=2LaGl8) for more info on eggsfs. Right now we have:

* A prototype server implementation in Python;
* A basic command line client in Python;
* A FUSE driver in Python;
* Garbage collector in Go.

## Starting the Python server

```
% ./python/pyeggsfs.py <data-dir>
```

The above will run all the processes needed to run EggsFS. This includes:

* 256 metadata shards;
* 1 cross directory coordinator (CDC)
* 10 block services (this is tunable with `--block_services`)
* 1 shuckle

A multitude of SQLite databases and directories to store the blocks will be stored in `<data-dir>`.

The services can also be started separatedly with `./shard.py`, `./cdc.py`, `./block_service.py`, and `./shuckle.py`. They are all needed to operate a filesystem, and the shard _must_ be 256.

With shuckle running, you can navigate to http://localhost:5000 to see a view of all block services.

## Mounting EggsFS using FUSE

```
% ./python/eggsfuse.py <mount-point>
```

Now you can use EggsFS normally. You can use `--debug` and `--debug-fuse` for debugging output.


Note that EggsFS' files are immutable! You'll get `EROFS` ("read only filesystem") if you try to modify files. In general, you'll have success writing files by opening them, writing to them in an append-only manner, and then closing them.

Sometimes you might get errors which are a bit more surprising than `EROFS`:

```
% echo blah > foobar
echo: write error: no such file or directory
```

The reason for this is that `foobar` is created by the shell and then opened again before being closed. This means that `foobar` does not exist yet, and you get the above. After this failure `foobar` _will_ be close and will materialized, as empty.

## Using the command line client


```
% ./python/basic_client.py ls /
% ./python/basic_client.py copy_into /cdc.py cdc.py # copies the contents of cdc.py into the EggsFS file /cdc.py
% ./python/basic_client.py cat /cdc.py # prints out the contents of cdc.py
% ./python/basic_client.py mkdir /blah
% ./python/basic_client.py echo_into /blah/baz 'hello' # creates file /blah/baz with string 'hello' in it
```

## Testing

```
./tests.sh
```

## Garbage Collection

Run

```
go run .
```

in `go/gc/gcdaemon` to run the garbage collector. It'll loop through directories removing what it consider stale files and stale directories.

You can also run the cli in `go/cli` to explicitly garbage collect a directory or a transient file. You can use `basic_client.py` to see what transient files there are (`./basic_client.py transient_files`).