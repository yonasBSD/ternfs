use bincode::Options;
use std::vec::Vec;
use std::string::String;
use std::net::UdpSocket;
use std::time::Duration;

#[path="../hex.rs"]
mod hex;
#[path="../metadata_msgs.rs"]
mod metadata_msgs;
#[path="../schema.rs"]
mod schema;


use metadata_msgs::*;


// this root id is ineffecient to encode
const ROOT_ID: u64 = !0u64;

const UDP_MTU: usize = 1472;

const LOCAL_HOST: std::net::IpAddr = std::net::IpAddr::V4(
    std::net::Ipv4Addr::new(127, 0, 0, 1));


fn generate_request_id() -> u64 {
    let now = std::time::SystemTime::now();
    let now_ns = now.duration_since(std::time::SystemTime::UNIX_EPOCH
        ).expect("Now is less than UNIX_EPOCH!").as_nanos();
    now_ns as u64
}


fn resolve(parent_id: u64, subname: String, ts: u64
    ) -> MetadataResult<Option<ResolvedInode>> {

    let request_id = generate_request_id();

    let port = schema::shard(parent_id) as u16 + 22272;
    let target = std::net::SocketAddr::new(LOCAL_HOST, port);
    let msg = MetadataRequest {
        request_id: request_id,
        body: MetadataRequestBody::Resolve{
            parent_id: parent_id, subname: subname, ts: ts,
        }
    };
    let encoded = bincode_opt().serialize(&msg).expect("Failed to encode msg");
    let sock = UdpSocket::bind("127.0.0.1:0").expect(
        "Could not bind UDP 127.0.0.1:0");
    sock.send_to(&encoded, &target).expect("Failed to send msg");

    let mut buf = [0u8; UDP_MTU];

    match sock.recv(&mut buf) {
        Ok(len) => {
            let res = bincode_opt().deserialize::<MetadataResponse>(
                &buf[..len]);
            match res {
                Ok(response) => {
                    if response.request_id != request_id {
                        Err(MetadataError{
                            kind: MetadataErrorKind::LogicError,
                            text: format!("Request id expected: {} got: {}",
                                request_id, response.request_id)
                        })
                    } else {
                        let b = response.body?;
                        match b {
                            //todo: dis
                            MetadataResponseBody::Resolve(result) => {
                                Ok(result)
                            },
                            _ => {
                                let s = format!(
                                    "Received unexpected response body {:?}",
                                    b);
                                Err(MetadataError{
                                    kind: MetadataErrorKind::LogicError,
                                    text: s,
                                })
                            }
                        }
                    }
                },
                Err(e) => {
                    let s = format!("Error deserializing {}\n{}", e,
                        hex::hexdump(&buf[..len]));
                    Err(MetadataError{
                        kind: MetadataErrorKind::BincodeError,
                        text: s,
                    })
                },
            }
        },
        Err(e) => {
            let s = format!("Error receiving reply from {}: {}", target, e);
            Err(MetadataError{
                kind: MetadataErrorKind::NetworkError,
                text: s,
            })
        },
    }
}


fn resolve_abs_path(path: &str, ts: u64) -> MetadataResult<ResolvedInode> {

    let bits: Vec<&str> = path.split('/').collect();

    if bits[0] != "" {
        return Err(MetadataError{
            kind: MetadataErrorKind::LogicError,
            text: String::from("Path must start with /"),
        });
    }

    let mut parent_inode = ResolvedInode {
        id: ROOT_ID,
        creation_time: 0,
        deletion_time: 0,
        is_file: false,
    };
    for (i, bit) in bits[1..].iter().enumerate() {
        if parent_inode.is_file {
            let filename = bits[..i+1].join("/");
            let s = format!("{} is a file", filename);
            return Err(MetadataError{
                kind: MetadataErrorKind::LogicError,
                text: s,
            });
        }

        let result = resolve(parent_inode.id, bit.to_string(), ts)?;

        match result {
            None => {
                let subdirname = bits[..i+2].join("/");
                let s = format!("No such file or directory {}", subdirname);
                return Err(MetadataError{
                    kind: MetadataErrorKind::LogicError,
                    text: s,
                });
            },
            Some(inode) => {
                parent_inode = inode;
            }
        }
    }

    Ok(parent_inode)
}


fn main() {
    let args: Vec<String> = std::env::args().collect();

    if args.len() < 3 {
        eprintln!("Usage: {} (resolve|ls|mkdir|rmdir) path [creation_ts]",
            args[0]);
        std::process::exit(1);
    }

    let command = &args[1];

    let path = &args[2];
    let creation_time = if args.len() >= 4 {
        Some(args[3].parse::<u64>().expect(
            "Couldn't parse creation_time as int"))
    } else {
        None
    };
    if command == "rmdir" && creation_time.is_none() {
        panic!("rmdir command requires a creation time");
    }
    let final_slash = path.rfind('/').expect("Path must contain /");
    let basename = &path[final_slash+1..];

    let resolve_path = if command == "mkdir" || path == "/" {
        // for mkdir need to resolve the parent
        &path[..final_slash]
    } else {
        &path
    };

    let sock = UdpSocket::bind("127.0.0.1:0").expect(
        "Could not bind UDP 127.0.0.1:0");

    sock.set_read_timeout(Some(Duration::new(2, 0))).expect(
        "Could not set read_timeout");

    let result = resolve_abs_path(&resolve_path, !0u64);
    let resolved_inode = match result {
        Ok(resolved) => resolved,
        Err(e) => {
            eprintln!("Failed to resolve {}: {:?}", resolve_path, e);
            std::process::exit(2);
        }
    };

    let shard = schema::shard(resolved_inode.id);

    let pretty_resolve_path = if resolve_path == "" {
        "/"
    } else {
        resolve_path
    };

    println!("'{}' resolved to:\n{:#?}", pretty_resolve_path,
        resolved_inode);

    let body = match command.as_str() {
        "resolve" => {
            // nothing else needs doing, we're good
            return;
        },
        "rmdir" => MetadataRequestBody::RmDir{
            parent_id: resolved_inode.id,
            subdirname: String::from(basename),
            creation_time: creation_time.unwrap(),
        },
        "mkdir" => MetadataRequestBody::MkDir{
            parent_id: resolved_inode.id,
            subdirname: String::from(basename),
        },
        _ => panic!("Command {} not supported yet", command),
    };

    let msg = MetadataRequest{
        request_id: generate_request_id(),
        body: body,
    };

    let port = shard as u16 + 22272;
    let target = std::net::SocketAddr::new(LOCAL_HOST, port);

    let encoded = bincode_opt().serialize(&msg).expect("Failed to encode msg");
    sock.send_to(&encoded, &target).expect("Failed to send msg");

    let mut buf = [0u8; UDP_MTU];
    match sock.recv_from(&mut buf) {
        Ok((len, src_addr)) => {
            let res = bincode_opt().deserialize::<MetadataResponse>(
                &buf[..len]);
            match res {
                Err(e) => println!("Failed to decode reply, reason {} raw:\n{}",
                    e, hex::hexdump(&buf[..len])),
                Ok(r) => println!("Got reply from {}:\n{:#?}", src_addr, r),
            }
        },
        Err(e) => println!("Error receiving reply from {}: {}", target, e),
    }
}
