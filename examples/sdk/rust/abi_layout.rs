#![allow(dead_code)]

#[repr(C)]
struct GlesContextDesc { structure_size:u32, requested_abi:u32, width:u32, height:u32, flags:u32, reserved:[u32;3] }
#[repr(C)]
struct GlesCapabilities { structure_size:u32, abi_version:u32, backend_flags:u32, max_width:u32, max_height:u32, reserved:[u32;3] }
#[repr(C)]
struct GlesProtocol { structure_size:u32, abi_version:u32, query:usize, create:usize, destroy:usize, make_current:usize, get_proc_address:usize, present:usize }
#[repr(C)]
struct NetSessionDesc { structure_size:u32, requested_abi:u32, byte_quota:u64, operation_quota:u32, reserved:u32 }
#[repr(C)]
struct AudioVoiceDesc { structure_size:u32, requested_abi:u32, sample_rate:u32, channels:u16, sample_bits:u16, gain_q16:i32, pan_q16:i32, byte_quota:u64 }

const _: () = assert!(core::mem::size_of::<GlesContextDesc>() == 32);
const _: () = assert!(core::mem::size_of::<GlesCapabilities>() == 32);
const _: () = assert!(core::mem::size_of::<GlesProtocol>() == 56);
const _: () = assert!(core::mem::size_of::<NetSessionDesc>() == 24);
const _: () = assert!(core::mem::size_of::<AudioVoiceDesc>() == 32);

fn main() { println!("PASS: Rust OpenConsole ABI layout"); }
