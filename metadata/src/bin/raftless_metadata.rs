use bincode::Options;
use rocksdb;
use std::fmt::Debug;
use serde::{Serialize, Deserialize};

#[path="../hex.rs"]
mod hex;
#[path="../metadata_msgs.rs"]
mod metadata_msgs;

use metadata_msgs::*;

// TODO: support jumbo frames (~8192 bytes)
const UDP_MTU: usize = 1472;


const PROTOCOL_VERSION: u32 = 0;


#[derive(Debug, Serialize, Deserialize)]
struct InodeTableValue {
    id: u64,
    deletion_time: u64,
    is_file: bool,
}


const FIXED_KEY_LEN: usize = 1 + 8 + 1 + 8;


fn make_key(parent_id: u64, subname: &str, ts: u64) -> Vec<u8> {
    // key format: b'D' + parent_id + subname + ts
    // subname is stored as a pascal string
    // which gives us desirable sorting properties
    // e.g. make_key(-1, "a", 0) < make_key(-1, "a", !0u64) < make_key(-1, "ab", 1234)
    let key_len = subname.len() + FIXED_KEY_LEN;
    let mut key = vec![0u8; key_len];
    key[0] = b'D';
    key[1..9].copy_from_slice(&parent_id.to_le_bytes());
    key[9] = subname.len() as u8;
    key[10..key_len-8].copy_from_slice(subname.as_bytes());
    key[key_len-8..].copy_from_slice(&ts.to_le_bytes());
    key
}


fn decompose_key<'a>(key: &'a [u8]) -> (u64, &'a str, u64) {
    let subname_len = key.len() - FIXED_KEY_LEN;

    assert!(key.len() >= FIXED_KEY_LEN, "Runt key:\n{}", hex::hexdump(key));
    assert!(subname_len == (key[9] as usize), "Inconsistent size ({} vs {})",
        subname_len, key[9] as usize);
    assert!(key[0] == b'D');

    let parent_id = u64::from_le_bytes(key[1..9].try_into().unwrap());

    let subname_raw = &key[10..subname_len+10];

    debug_assert!(std::str::from_utf8(subname_raw).is_ok(),
        "Bad key in db: 0x{}", hex::hexstr(key));

    let subname = unsafe { std::str::from_utf8_unchecked(subname_raw) };

    let creation_ts = u64::from_le_bytes(
        key[key.len()-8..].try_into().unwrap());

    (parent_id, subname, creation_ts)
}


// on success returns the creation time
fn do_mkdir(parent_id: u64, next_id: &mut u64, subname: &str, db: &rocksdb::DB
    ) -> MetadataResult<MetadataResponseBody> {

    if subname.len() > 255 {
        let text = format!("Subname len {} too long, max 255", subname.len());
        return Err(MetadataError{
            kind: MetadataErrorKind::NameTooLong,
            text: text,
        });
    }

    let this_id = *next_id;

    let now = std::time::SystemTime::now();
    let creation_time = now.duration_since(std::time::SystemTime::UNIX_EPOCH
        ).expect("Now is less than UNIX_EPOCH!").as_secs();

    let mut key = make_key(parent_id, subname, !0u64);

    let key_lower_bound = make_key(parent_id, subname, 0);

    let mut read_options = rocksdb::ReadOptions::default();
    // N.B. "`iterate_lower_bound` is inclusive"
    read_options.set_iterate_lower_bound(key_lower_bound);

    let iter_mode = rocksdb::IteratorMode::From(&key,
        rocksdb::Direction::Reverse);

    let result = db.iterator_opt(iter_mode, read_options).next();

    // if there are already an entries with the same subname
    // ensure the latest one is deleted
    if let Some((key, value)) = result {
        let key_len = key.len();
        let existing_creation_ts = u64::from_le_bytes(
            key[key_len-8..].try_into().unwrap());

        // disallow if the existing one was created recently
        if existing_creation_ts + 10 > creation_time {
            let text = format!("Name {} was created recently ({} seconds ago)",
                subname, creation_time as i64 - existing_creation_ts as i64);
            return Err(MetadataError{
                kind: MetadataErrorKind::TooSoon,
                text: text,
            });
        }

        let decoded_val: InodeTableValue = bincode_opt().deserialize(&value
            ).expect("Bad value in db");

        if decoded_val.deletion_time == 0 {
            let text = format!("Inode '{}' ts {} exists and is not deleted",
                subname, existing_creation_ts);
            return Err(MetadataError{
                kind: MetadataErrorKind::InodeAlreadyExists,
                text: text,
            });
        }
    }

    let key_len = key.len();
    key[key_len-8..].copy_from_slice(&creation_time.to_le_bytes());

    let new_val = InodeTableValue {
        id: this_id,
        deletion_time: 0,
        is_file: false,
    };

    let serialized_val = bincode_opt().serialize(&new_val).expect(
        "Could not serialze value");

    let mut batch = rocksdb::WriteBatch::default();
    batch.put(key, serialized_val);
    batch.put(b"M_LAST_INODE_ID", this_id.to_le_bytes());
    if let Err(e) = db.write(batch) {
        return Err(MetadataError{
            kind: MetadataErrorKind::RocksDbError,
            text: format!("Error writing to rocks: {:?}", e),
        });
    }

    // LSB is creator shard
    *next_id += 0x100;

    Ok(MetadataResponseBody::MkDir(ResolvedInode{
        id: this_id,
        creation_time: creation_time,
        deletion_time: 0,
        is_file: false,
    }))
}


// on success returns creation_ts and value bytes
fn do_resolve(parent_id: u64, subname: &str, ts: u64, db: &rocksdb::DB
    ) -> MetadataResult<MetadataResponseBody> {

    // when querying "the present" set snapshot_ts to !0u64, as our upper bound
    let creation_ts_upper_bound = if ts == 0 { !0u64 } else { ts };

    let key_lower_bound = make_key(parent_id, subname, 0);
    let key_upper_bound = make_key(parent_id, subname, creation_ts_upper_bound);

    let iter_mode = rocksdb::IteratorMode::From(&key_upper_bound,
        rocksdb::Direction::Reverse);
    let mut read_options = rocksdb::ReadOptions::default();
    // N.B. "`iterate_lower_bound` is inclusive"
    read_options.set_iterate_lower_bound(key_lower_bound);

    let result = db.iterator_opt(iter_mode, read_options).next();

    let maybe_resolved = match result {
        None => {
            None
        },
        Some((key, raw_value)) => {
            let key_len = key.len();
            let creation_time = u64::from_le_bytes(
                key[key_len-8..].try_into().unwrap());
            let value: InodeTableValue = bincode_opt().deserialize(
                &raw_value).expect("Bad value in db");
            let response = ResolvedInode {
                id: value.id,
                creation_time: creation_time,
                deletion_time: value.deletion_time,
                is_file: value.is_file,
            };
            Some(response)
        },
    };

    Ok(MetadataResponseBody::Resolve(maybe_resolved))
}


// fn do_lsdir(id: u64, from: &str, ts: u64, db: &rocksdb::DB
//     ) -> MetadataResult<MetadataResponseBody> {

//     let key_lower_bound = make_key(id, from, 0);
//     let key_upper_bound = make_key(id + 1, "", 0);

//     let mut read_options = rocksdb::ReadOptions::default();
//     read_options.set_iterate_upper_bound(key_upper_bound);

//     let mut iter = db.raw_iterator_opt(read_options);

//     iter.seek(key_lower_bound);

//     while let Some(key) = iter.key() {

//     }

//     if let Err(e) = iter.status() {
//         Err(MetadataError{
//             kind: MetadataErrorKind::RocksDbError,
//             text: format!("{}", e),
//         })
//     }


// }


fn main() {
    let args: Vec<String> = std::env::args().collect();

    if args.len() < 2 {
        println!("Usage: {} shard", args[0]);
        std::process::exit(1);
    }

    let shard: u8 = args[1].parse().expect("Failed to parse shard");

    let db = rocksdb::DB::open_default(
        format!("/home/jchicke/playground/fs/{}", shard)).expect(
            "Could not load db");

    let mut next_inode_id = match db.get(b"M_LAST_INODE_ID").unwrap() {
        None => {
            if shard == 0 {
                // id 0 is reserved for root
                1u64 << 8
            } else {
                shard as u64
            }
        },
        Some(v) => {
            let last_id = u64::from_le_bytes(v.try_into().expect(
                "Bad format for M_LAST_INODE_ID"));
            last_id + 0x100 // LSB is creator shard
        }
    };

    println!("Using {:#018X} as next inode id", next_inode_id);

    let port: u16 = shard as u16 + 22272;

    let addr = std::net::SocketAddr::new(
        std::net::IpAddr::V4(std::net::Ipv4Addr::new(127, 0, 0, 1)), port);

    let sock = std::net::UdpSocket::bind(addr).expect("Could not bind UDP");

    loop {
        let mut buf = [0u8; UDP_MTU];
        let (len, origin) = sock.recv_from(&mut buf).expect("Recv failed");
        match bincode_opt().deserialize::<MetadataRequest>(&buf[..len]) {
            Err(e) => eprintln!("Failed to decode message with error {}\n{}", e,
                hex::hexdump(&buf[..len])),
            Ok(m) => {
                println!("Got {:?} from {}", m, origin);
                let response_body = if m.ver > PROTOCOL_VERSION {
                    Err(MetadataError {
                        kind: MetadataErrorKind::UnsupportedVersion,
                        text: format!(
                            "Unsupported ver ({} vs {}) on request",
                            m.ver, PROTOCOL_VERSION),
                    })
                } else {
                    match m.body {
                        MetadataRequestBody::Resolve { parent_id, subname, ts }
                            => do_resolve(parent_id, &subname, ts, &db),
                        MetadataRequestBody::MkDir { parent_id, subdirname }
                            => do_mkdir(parent_id, &mut next_inode_id, &subdirname,
                                &db),
                        _ => {
                            eprintln!("Command not supported yet, ignoring");
                            continue;
                        },
                    }
                };
                let response = MetadataResponse{
                    request_id: m.request_id,
                    body: response_body,
                };
                let response_sz = bincode_opt().serialized_size(&response
                    ).unwrap() as usize;
                if response_sz > UDP_MTU {
                    eprintln!("response {:?} is too big for UDP_MTU {}",
                        response, UDP_MTU);
                }
                let res0 = bincode_opt().serialize_into(&mut buf[..],
                    &response);
                if let Err(e) = res0 {
                    eprintln!("Failed to Serialise\n{:#?}, reason: {}",
                        response, e);
                }
                let res1 = sock.send_to(&buf[..response_sz], origin);
                if let Err(e) = res1 {
                    eprintln!("Failed to send to {}, reason: {}", origin,
                        e);
                }
            },
        };

    }
}
