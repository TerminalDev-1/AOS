#![no_std]
#![no_main]

extern crate alloc;

use alloc::vec;
use boot_protocol::{BOOT_INFO_VERSION, BootInfo};
use core::ptr::{copy_nonoverlapping, write_bytes};
use core::time::Duration;
use uefi::mem::memory_map::{MemoryMap, MemoryType};
use uefi::prelude::*;
use uefi::proto::console::gop::{BltOp, BltPixel, BltRegion, GraphicsOutput};
use uefi::{Status, boot};

const KERNEL: &[u8] = include_bytes!("../out/kernel.elf");
const SEED: &[u8] = include_bytes!("../out/seed.img");
const PAGE: usize = 4096;
const BOOT_LOGO: &[u8] = include_bytes!("../kernel/assets/boot-logo.bin");

fn render_boot_logo(graphics: &mut GraphicsOutput, width: usize, height: usize) {
    let _ = graphics.blt(BltOp::VideoFill {
        color: BltPixel::new(13, 18, 30), dest: (0, 0), dims: (width, height),
    });
    let logo_width = u16::from_le_bytes([BOOT_LOGO[0], BOOT_LOGO[1]]) as usize;
    let logo_height = u16::from_le_bytes([BOOT_LOGO[2], BOOT_LOGO[3]]) as usize;
    let display_width = (width * 4 / 5).min(height * 3 * logo_width / (5 * logo_height)).max(2);
    let display_height = (display_width * logo_height / logo_width).max(2);
    let mut pixels = vec![BltPixel::new(13, 18, 30); display_width * display_height];
    for y in 0..display_height {
        let sy = y * (logo_height - 1) * 256 / (display_height - 1);
        let y0 = sy / 256;
        let y1 = (y0 + 1).min(logo_height - 1);
        let fy = (sy % 256) as u32;
        for x in 0..display_width {
            let sx = x * (logo_width - 1) * 256 / (display_width - 1);
            let x0 = sx / 256;
            let x1 = (x0 + 1).min(logo_width - 1);
            let fx = (sx % 256) as u32;
            let a = |x: usize, y: usize| BOOT_LOGO[4 + y * logo_width + x] as u32;
            let alpha = ((a(x0, y0) * (256 - fx) + a(x1, y0) * fx) * (256 - fy)
                         + (a(x0, y1) * (256 - fx) + a(x1, y1) * fx) * fy) / 65536;
            let mix = |background: u32, foreground: u32| -> u8 {
                ((background * (255 - alpha) + foreground * alpha) / 255) as u8
            };
            pixels[y * display_width + x] = BltPixel::new(mix(13, 235), mix(18, 52), mix(30, 65));
        }
    }
    let _ = graphics.blt(BltOp::BufferToVideo {
        buffer: &pixels, src: BltRegion::Full,
        dest: (width.saturating_sub(display_width) / 2,
               height.saturating_sub(display_height) / 2),
        dims: (display_width, display_height),
    });
    boot::stall(Duration::from_millis(1200));
}

fn u16_at(bytes: &[u8], offset: usize) -> Option<u16> {
    Some(u16::from_le_bytes(bytes.get(offset..offset + 2)?.try_into().ok()?))
}

fn u32_at(bytes: &[u8], offset: usize) -> Option<u32> {
    Some(u32::from_le_bytes(bytes.get(offset..offset + 4)?.try_into().ok()?))
}

fn u64_at(bytes: &[u8], offset: usize) -> Option<u64> {
    Some(u64::from_le_bytes(bytes.get(offset..offset + 8)?.try_into().ok()?))
}

fn load_kernel() -> Result<usize, Status> {
    let elf = KERNEL;
    if elf.get(0..4) != Some(&b"\x7fELF"[..]) || elf.get(4) != Some(&2)
        || elf.get(5) != Some(&1) || u16_at(elf, 18) != Some(62)
    {
        return Err(Status::LOAD_ERROR);
    }
    let phoff = u64_at(elf, 32).ok_or(Status::LOAD_ERROR)? as usize;
    let phsize = u16_at(elf, 54).ok_or(Status::LOAD_ERROR)? as usize;
    let phnum = u16_at(elf, 56).ok_or(Status::LOAD_ERROR)? as usize;
    let entry = u64_at(elf, 24).ok_or(Status::LOAD_ERROR)? as usize;
    if phsize < 56 || phnum == 0 {
        return Err(Status::LOAD_ERROR);
    }
    let mut start = usize::MAX;
    let mut end = 0usize;
    for i in 0..phnum {
        let ph = phoff.checked_add(i.checked_mul(phsize).ok_or(Status::LOAD_ERROR)?)
            .ok_or(Status::LOAD_ERROR)?;
        if u32_at(elf, ph) != Some(1) {
            continue;
        }
        let address = u64_at(elf, ph + 16).ok_or(Status::LOAD_ERROR)? as usize;
        let memory_size = u64_at(elf, ph + 40).ok_or(Status::LOAD_ERROR)? as usize;
        start = start.min(address & !(PAGE - 1));
        end = end.max(address.checked_add(memory_size).ok_or(Status::LOAD_ERROR)?);
    }
    if start == usize::MAX || entry < start || entry >= end {
        return Err(Status::LOAD_ERROR);
    }
    let page_end = end.checked_add(PAGE - 1).ok_or(Status::LOAD_ERROR)? & !(PAGE - 1);
    let pages = (page_end - start) / PAGE;
    boot::allocate_pages(boot::AllocateType::Address(start as u64),
                         MemoryType::LOADER_DATA, pages).map_err(|e| e.status())?;
    unsafe { write_bytes(start as *mut u8, 0, pages * PAGE) };
    for i in 0..phnum {
        let ph = phoff + i * phsize;
        if u32_at(elf, ph) != Some(1) {
            continue;
        }
        let file_offset = u64_at(elf, ph + 8).ok_or(Status::LOAD_ERROR)? as usize;
        let address = u64_at(elf, ph + 16).ok_or(Status::LOAD_ERROR)? as usize;
        let file_size = u64_at(elf, ph + 32).ok_or(Status::LOAD_ERROR)? as usize;
        let memory_size = u64_at(elf, ph + 40).ok_or(Status::LOAD_ERROR)? as usize;
        if file_size > memory_size || address < start || address + memory_size > page_end {
            return Err(Status::LOAD_ERROR);
        }
        let data = elf.get(file_offset..file_offset + file_size).ok_or(Status::LOAD_ERROR)?;
        unsafe { copy_nonoverlapping(data.as_ptr(), address as *mut u8, data.len()) };
    }
    Ok(entry)
}

#[entry]
fn main() -> Status {
    if uefi::helpers::init().is_err() {
        return Status::ABORTED;
    }
    uefi::println!("AOS: displaying boot logo before kernel load");
    let Ok(handle) = boot::get_handle_for_protocol::<GraphicsOutput>() else {
        return Status::UNSUPPORTED;
    };
    let Ok(mut graphics) = boot::open_protocol_exclusive::<GraphicsOutput>(handle) else {
        return Status::DEVICE_ERROR;
    };
    let mode = graphics.current_mode_info();
    let (width, height) = mode.resolution();
    render_boot_logo(&mut graphics, width, height);
    let entry = match load_kernel() {
        Ok(entry) => entry,
        Err(status) => {
            uefi::println!("AOS: kernel ELF load failed: {:?}", status);
            return status;
        }
    };
    let Ok(ramdisk) = boot::allocate_pages(boot::AllocateType::AnyPages,
                                          MemoryType::LOADER_DATA,
                                          SEED.len().div_ceil(PAGE)) else {
        return Status::OUT_OF_RESOURCES;
    };
    unsafe { copy_nonoverlapping(SEED.as_ptr(), ramdisk.as_ptr(), SEED.len()) };
    let mut framebuffer = graphics.frame_buffer();
    let mut info = BootInfo {
        version: BOOT_INFO_VERSION,
        pixel_format: mode.pixel_format() as u32,
        framebuffer: framebuffer.as_mut_ptr(),
        framebuffer_size: framebuffer.size(),
        width,
        height,
        stride: mode.stride(),
        memory_map: core::ptr::null(),
        memory_map_size: 0,
        descriptor_size: 0,
        ramdisk: ramdisk.as_ptr(),
        ramdisk_size: SEED.len(),
    };
    drop(framebuffer);
    drop(graphics);
    let Ok(stack) = boot::allocate_pages(boot::AllocateType::AnyPages,
                                          MemoryType::LOADER_DATA, 16) else {
        return Status::OUT_OF_RESOURCES;
    };
    let stack_top = (stack.as_ptr() as usize + 16 * PAGE - 8) & !15usize | 8;
    unsafe { (stack_top as *mut usize).write(0) };

    // The final map is allocated before ExitBootServices by the uefi crate.
    // No firmware calls or allocations occur after this point.
    let memory_map = unsafe { boot::exit_boot_services(None) };
    info.memory_map = memory_map.buffer().as_ptr();
    info.memory_map_size = memory_map.buffer().len();
    info.descriptor_size = memory_map.meta().desc_size;
    unsafe {
        core::arch::asm!(
            "mov rsp, {stack}",
            "xor rbp, rbp",
            "jmp {entry}",
            stack = in(reg) stack_top,
            entry = in(reg) entry,
            in("rdi") &info,
            options(noreturn)
        );
    }
}
