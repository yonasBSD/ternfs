use serde::{Serialize, Deserialize};
use std::string::String;
use bincode;


pub fn bincode_opt() -> bincode::DefaultOptions {
    bincode::DefaultOptions::new()
}


#[derive(Serialize, Deserialize, Debug)]
pub enum MetadataRequestBody {
    CreateBlock,
    DeleteBlockRequest,
    DeleteBlockComplete,
    ReadBlockMap,
    CreateFile,
    DeleteFile,
    PurgeFileRequest,
    PurgeFileComplete,
    MvFile,
    LsDir,
    MkDir{ parent_id: u64, subdirname: String },
    // creation_time on RmDir ensures it's idempotent
    RmDir{ parent_id: u64, subdirname: String, creation_time: u64 },
    MvDir,
    // if you're looking at "the present" use 0 as ts
    // otherwise populate it with snapshot ts
    // (0 chosen due to efficient varint encoding)
    Resolve{ parent_id: u64, subname: String, ts: u64 },
}


#[derive(Serialize, Deserialize, Debug)]
pub struct MetadataRequest {
    pub ver: u32,
    pub request_id: u32, // echoed back to the client in response
                         // client's responsibiltity to keep unique
    pub body: MetadataRequestBody,
}


#[derive(Serialize, Deserialize, Debug)]
pub struct ResolvedInode {
    pub id: u64,
    pub creation_time: u64,
    pub deletion_time: u64, // 0 => not deleted
    pub is_file: bool,
}


#[derive(Serialize, Deserialize, Debug)]
pub enum MetadataErrorKind {
    TooSoon,
    InodeAlreadyExists,
    NameTooLong,
    RocksDbError,
    NetworkError,
    BincodeError,
    LogicError,
    UnsupportedVersion,
}


#[derive(Serialize, Deserialize, Debug)]
pub struct MetadataError {
    pub kind: MetadataErrorKind,
    pub text: String,
}


pub type MetadataResult<T> = Result<T, MetadataError>;


#[derive(Serialize, Deserialize, Debug)]
pub enum MetadataResponseBody {
    MkDir(ResolvedInode),
    Resolve(Option<ResolvedInode>),
}


#[derive(Serialize, Deserialize, Debug)]
pub struct MetadataResponse {
    pub request_id: u32, // echoed back from request
    pub body: MetadataResult<MetadataResponseBody>,
}
