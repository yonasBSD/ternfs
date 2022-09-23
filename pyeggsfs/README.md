# pyeggsfs

Pyeggsfs is the python prototype of eggsfs. See the [proposal](https://xtxmarketscom.sharepoint.com/:w:/s/ECN/EdVNBAzB7klPsVw6CxkfAvwB0LGu4pbtf-Gafr0tMnWNKw?e=2LaGl8) for more info on eggsfs.

## Usage

```
% ./eggs.py <data-dir>
```

The above will run all the processes needed to run EggsFS. This includes:

* 256 metadata shards;
* 1 cross directory coordinator (CDC)
* 10 block services (this is tunable with `--block_services`)
* 1 shuckle

A multitude of SQLite databases and directories to store the blocks will be stored in `<data-dir>`.

Once started, you can start poking at the filesystem using `basic_client.py`, e.g.

```
% ./basic_client.py ls /
% ./basic_client.py copy_into /cdc.py cdc.py # copies the contents of cdc.py into the EggsFS file /cdc.py
% ./basic_client.py cat /cdc.py # prints out the contents of cdc.py
% ./basic_client.py mkdir /blah
% ./basic_client.py echo_into /blah/baz 'hello' # creates file /blah/baz with string 'hello' in it

The services can also be started separatedly with `./shard.py`, `./cdc.py`, `./block_service.py`, and `./shuckle.py`. They are all needed to operate a filesystem, and the shard _must_ be 256.

With shuckle running, you can navigate to http://localhost:5000 to see a view of all block services.

## Testing

```
./tests.py
```

We try to test all operations and all error conditions. The CDC test driver can inject shard failures to test cases where shard requests go wrong.