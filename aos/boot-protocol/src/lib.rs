#![no_std]

pub const BOOT_INFO_VERSION: u32 = 2;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct BootInfo {
    pub version: u32,
    /// UEFI GOP pixel format: 0=RGB, 1=BGR, 2=bitmask, 3=blit-only.
    pub pixel_format: u32,
    pub framebuffer: *mut u8,
    pub framebuffer_size: usize,
    pub width: usize,
    pub height: usize,
    pub stride: usize,
    pub memory_map: *const u8,
    pub memory_map_size: usize,
    pub descriptor_size: usize,
    /// A clean writable-in-RAM FAT12 seed, used when no ATA data disk exists.
    pub ramdisk: *mut u8,
    pub ramdisk_size: usize,
}
