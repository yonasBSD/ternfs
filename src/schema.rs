use core::arch::x86_64;


pub fn shard(inode_id: u64) -> u8 {
    let ret = unsafe {
        x86_64::_mm_crc32_u64(0, inode_id)
    };
    ret as u8
}
