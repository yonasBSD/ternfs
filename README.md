**BEWARE!** What's below is already outdated, sadly. If you want to try out EggsFS, ask fmazzol.

# EggsFS

See the [proposal](https://xtxmarketscom.sharepoint.com/:w:/s/ECN/EdVNBAzB7klPsVw6CxkfAvwB0LGu4pbtf-Gafr0tMnWNKw?e=2LaGl8) for more info on eggsfs.

This repo currently contains these components:

* Shard implementation (`eggs-shard` Makefile target in `cpp/`);
* CDC implementation (`eggs-cdc` Makefile target in `cpp/`);
* Garbage collection daemon (`go/gcdaemon`);
* A very basic shuckle prototype (`go/shuckle`);
* FUSE driver prototype (`python/eggsfuse.py`);
* A simple client (`python/basic_client.py`);
* A block service prototype (`python/block_service.py`);
* Some convenience Go code to run a full EggsFS instance easily (`go/runeggs`);

We plan to purge the remaining Python code and be left with only C++ and Go.

## Starting an EggsFS instance

```
% cd go/runeggs
% go run . -dir <data-dir>
```
The above will run all the processes needed to run EggsFS. This includes:

* 256 metadata shards;
* 1 cross directory coordinator (CDC)
* A bunch of block services (this is tunable with the `-flash-block-services` and `-hdd-block-services` flags)
* 1 shuckle instance

A multitude of directories to persist the whole thing will appear in `<data-dir>`.


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
% cd go/gc/gcdaemon
% go run .
```

in `go/gc/gcdaemon` to run the garbage collector. It'll loop through directories removing what it consider stale files and stale directories.

You can also run the cli in `go/cli` to explicitly garbage collect a directory or a transient file. You can use `basic_client.py` to see what transient files there are (`./basic_client.py transient_files`).