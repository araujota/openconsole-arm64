const GlesContextDesc = extern struct {
    structure_size: u32, requested_abi: u32, width: u32, height: u32, flags: u32,
    reserved: [3]u32,
};
const GlesCapabilities = extern struct {
    structure_size: u32, abi_version: u32, backend_flags: u32, max_width: u32,
    max_height: u32, reserved: [3]u32,
};
const GlesProtocol = extern struct {
    structure_size: u32, abi_version: u32,
    query: usize, create: usize, destroy: usize, make_current: usize,
    get_proc_address: usize, present: usize,
};
const NetSessionDesc = extern struct {
    structure_size: u32, requested_abi: u32, byte_quota: u64,
    operation_quota: u32, reserved: u32,
};
const AudioVoiceDesc = extern struct {
    structure_size: u32, requested_abi: u32, sample_rate: u32,
    channels: u16, sample_bits: u16, gain_q16: i32, pan_q16: i32, byte_quota: u64,
};

comptime {
    if (@sizeOf(GlesContextDesc) != 32) @compileError("GLES context ABI drift");
    if (@sizeOf(GlesCapabilities) != 32) @compileError("GLES caps ABI drift");
    if (@sizeOf(GlesProtocol) != 56) @compileError("GLES protocol ABI drift");
    if (@sizeOf(NetSessionDesc) != 24) @compileError("network ABI drift");
    if (@sizeOf(AudioVoiceDesc) != 32) @compileError("audio ABI drift");
}

pub fn main() void {}
