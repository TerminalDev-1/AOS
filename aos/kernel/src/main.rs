#![no_std]
#![no_main]

use boot_protocol::BootInfo;
use core::arch::asm;
use core::ptr::{copy_nonoverlapping, read_unaligned, write_bytes, write_volatile};
use core::sync::atomic::{AtomicBool, AtomicU64, AtomicUsize, Ordering};

mod user;

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    serial_write(b"AOS kernel panic\r\n");
    loop { core::hint::spin_loop(); }
}

static TICKS: AtomicU64 = AtomicU64::new(0);
static TASKBAR_FOCUS: AtomicU64 = AtomicU64::new(0);
const TASKBAR_TILE: usize = 56;
const TASKBAR_APPS_X: usize = 260;
const TASKBAR_BUFFER_WIDTH: usize = 8192;
const TASKBAR_BUFFER_HEIGHT: usize = 56;
static TASKBAR_DRAW: AtomicBool = AtomicBool::new(false);
static TASKBAR_ORIGIN_X: AtomicUsize = AtomicUsize::new(0);
static TASKBAR_ORIGIN_Y: AtomicUsize = AtomicUsize::new(0);
static SHELL_BUFFER_STRIDE: AtomicUsize = AtomicUsize::new(TASKBAR_BUFFER_WIDTH);
static SHELL_BUFFER_HEIGHT: AtomicUsize = AtomicUsize::new(TASKBAR_BUFFER_HEIGHT);
static mut TASKBAR_BUFFER: [u8; TASKBAR_BUFFER_WIDTH * TASKBAR_BUFFER_HEIGHT * 4] =
    [0; TASKBAR_BUFFER_WIDTH * TASKBAR_BUFFER_HEIGHT * 4];
static RAM_DISK_PTR: AtomicUsize = AtomicUsize::new(0);
static RAM_DISK_SIZE: AtomicUsize = AtomicUsize::new(0);
static USE_RAM_DISK: AtomicBool = AtomicBool::new(false);
static WINDOW_VISIBLE: AtomicBool = AtomicBool::new(true);
static WINDOW_MAXIMIZED: AtomicBool = AtomicBool::new(false);
static SHELL_ACTIVE: AtomicUsize = AtomicUsize::new(0);
static START_OPEN: AtomicBool = AtomicBool::new(false);
const START_BACKING_SIDE: usize = 576;
static START_BACKING_VALID: AtomicBool = AtomicBool::new(false);
static mut START_BACKING: [u8; START_BACKING_SIDE * START_BACKING_SIDE * 4] =
    [0; START_BACKING_SIDE * START_BACKING_SIDE * 4];
const WINDOW_BUTTON_WIDTH: usize = 46;

const CURSOR_WIDTH: usize = 24;
const CURSOR_HEIGHT: usize = 30;
const CURSOR_PIXELS: usize = CURSOR_WIDTH * CURSOR_HEIGHT;
const CURSOR_OUTLINE: [(i32, i32); 7] =
    [(1, 1), (1, 21), (6, 17), (11, 26), (15, 24), (10, 16), (21, 16)];
const CURSOR_INTERIOR: [(i32, i32); 7] =
    [(3, 4), (3, 18), (7, 14), (12, 23), (13, 22), (8, 14), (17, 14)];

#[derive(Clone, Copy)]
struct CursorInk { shadow: u8, outline: u8, interior: u8 }

fn cursor_contains(px: i32, py: i32, shape: &[(i32, i32); 7],
                   offset_x: i32, offset_y: i32) -> bool {
    let mut inside = false;
    for edge in 0..shape.len() {
        let (ax, ay) = shape[edge];
        let (bx, by) = shape[(edge + 1) % shape.len()];
        let ax = (ax + offset_x) * 4;
        let ay = (ay + offset_y) * 4;
        let bx = (bx + offset_x) * 4;
        let by = (by + offset_y) * 4;
        if (ay > py) != (by > py) {
            let left = (px - ax) * (by - ay);
            let right = (bx - ax) * (py - ay);
            if (by > ay && left < right) || (by < ay && left > right) {
                inside = !inside;
            }
        }
    }
    inside
}

fn cursor_coverage(x: usize, y: usize, shape: &[(i32, i32); 7],
                   offset_x: i32, offset_y: i32) -> u8 {
    let mut covered = 0u16;
    for sy in 0..4 {
        for sx in 0..4 {
            if cursor_contains((x * 4 + sx) as i32, (y * 4 + sy) as i32,
                               shape, offset_x, offset_y) {
                covered += 1;
            }
        }
    }
    (covered * 255 / 16) as u8
}

struct Mouse {
    x: usize,
    y: usize,
    packet: [u8; 3],
    index: usize,
    left_down: bool,
    backing: [u8; CURSOR_PIXELS * 4],
    rendered: [u8; CURSOR_PIXELS * 3],
    ink: [CursorInk; CURSOR_PIXELS],
    drawn: bool,
}

impl Mouse {
    fn new(info: &BootInfo) -> Self {
        let mut ink = [CursorInk { shadow: 0, outline: 0, interior: 0 }; CURSOR_PIXELS];
        for row in 0..CURSOR_HEIGHT {
            for col in 0..CURSOR_WIDTH {
                ink[row * CURSOR_WIDTH + col] = CursorInk {
                    shadow: cursor_coverage(col, row, &CURSOR_OUTLINE, 1, 2) / 3,
                    outline: cursor_coverage(col, row, &CURSOR_OUTLINE, 0, 0),
                    interior: cursor_coverage(col, row, &CURSOR_INTERIOR, 0, 0),
                };
            }
        }
        Self { x: info.width / 2, y: info.height / 2,
               packet: [0; 3], index: 0, left_down: false,
               backing: [0; CURSOR_PIXELS * 4],
               rendered: [0; CURSOR_PIXELS * 3], ink, drawn: false }
    }

    fn erase(&mut self, info: &BootInfo) {
        if !self.drawn { return; }
        for row in 0..CURSOR_HEIGHT {
            for col in 0..CURSOR_WIDTH {
                let index = row * CURSOR_WIDTH + col;
                let ink = self.ink[index];
                if ink.shadow == 0 && ink.outline == 0 && ink.interior == 0 { continue; }
                if self.x + col >= info.width || self.y + row >= info.height { continue; }
                let src = index * 4;
                let dest = ((self.y + row) * info.stride + self.x + col) * 4;
                if dest + 4 > info.framebuffer_size { continue; }
                let still_cursor = (0..3).all(|byte| unsafe {
                    core::ptr::read_volatile(info.framebuffer.add(dest + byte)) ==
                        self.rendered[index * 3 + byte]
                });
                if still_cursor {
                    for byte in 0..4 {
                        unsafe { write_volatile(info.framebuffer.add(dest + byte),
                                                 self.backing[src + byte]); }
                    }
                }
            }
        }
        self.drawn = false;
    }

    fn draw(&mut self, info: &BootInfo) {
        if self.drawn { return; }
        for row in 0..CURSOR_HEIGHT {
            for col in 0..CURSOR_WIDTH {
                let index = row * CURSOR_WIDTH + col;
                let ink = self.ink[index];
                if ink.shadow == 0 && ink.outline == 0 && ink.interior == 0 { continue; }
                if self.x + col >= info.width || self.y + row >= info.height { continue; }
                let src = index * 4;
                let dest = ((self.y + row) * info.stride + self.x + col) * 4;
                if dest + 4 > info.framebuffer_size { continue; }
                for byte in 0..4 {
                    self.backing[src + byte] = unsafe { core::ptr::read_volatile(info.framebuffer.add(dest + byte)) };
                }
                for channel in 0..3 {
                    let dark = if info.pixel_format == 1 {
                        [58u16, 39, 28][channel]
                    } else { [28u16, 39, 58][channel] };
                    let mut value = self.backing[src + channel] as u16;
                    for (colour, alpha) in [(20u16, ink.shadow), (dark, ink.outline),
                                             (255u16, ink.interior)] {
                        value = (value * (255 - alpha as u16) + colour * alpha as u16)
                            / 255;
                    }
                    let value = value as u8;
                    self.rendered[index * 3 + channel] = value;
                    unsafe { write_volatile(info.framebuffer.add(dest + channel), value); }
                }
            }
        }
        self.drawn = true;
    }

    fn packet_byte(&mut self, byte: u8, info: &BootInfo) -> bool {
        if self.index == 0 && byte & 0x08 == 0 { return false; }
        self.packet[self.index] = byte;
        self.index += 1;
        if self.index != 3 { return false; }
        self.index = 0;
        if self.packet[0] & 0xc0 == 0 {
            self.erase(info);
            let dx = self.packet[1] as i8 as isize;
            let dy = self.packet[2] as i8 as isize;
            self.x = self.x.saturating_add_signed(dx).min(info.width.saturating_sub(1));
            self.y = self.y.saturating_add_signed(-dy).min(info.height.saturating_sub(1));
            self.draw(info);
        }
        let pressed = self.packet[0] & 1 != 0;
        let click = pressed && !self.left_down;
        self.left_down = pressed;
        click
    }
}

core::arch::global_asm!(r#"
.global aos_timer_isr
aos_timer_isr:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov r15, rsp
    mov rdi, [r15 + 128]
    and rsp, -16
    call aos_timer_tick
    mov rsp, r15
    test al, al
    jnz aos_user_abort
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    iretq
"#);

unsafe extern "C" {
    fn aos_timer_isr();
}

#[unsafe(no_mangle)]
extern "sysv64" fn aos_timer_tick(interrupted_cs: u64) -> u8 {
    let now = TICKS.fetch_add(1, Ordering::Relaxed) + 1;
    outb(0x20, 0x20);
    user::timer_expired(now, interrupted_cs) as u8
}

#[repr(C, packed)]
#[derive(Clone, Copy)]
struct IdtEntry {
    offset_low: u16,
    selector: u16,
    ist: u8,
    flags: u8,
    offset_mid: u16,
    offset_high: u32,
    reserved: u32,
}

#[repr(C, packed)]
struct IdtPointer {
    limit: u16,
    base: u64,
}

#[unsafe(no_mangle)]
pub extern "sysv64" fn kernel_main(info_ptr: *const BootInfo) -> ! {
    unsafe { asm!("cli") };
    let info = unsafe { &*info_ptr };
    serial_init();
    if info.version != boot_protocol::BOOT_INFO_VERSION {
        serial_write(b"AOS boot protocol mismatch\r\n");
        loop { core::hint::spin_loop(); }
    }
    RAM_DISK_PTR.store(info.ramdisk as usize, Ordering::Relaxed);
    RAM_DISK_SIZE.store(info.ramdisk_size, Ordering::Relaxed);
    serial_write(b"AOS kernel entered after ExitBootServices\r\n");
    serial_write(b"Memory map bytes: 0x");
    serial_hex(info.memory_map_size as u64);
    serial_write(b"\r\n");

    let mut frames = FrameAllocator::new(info);
    let page_tables = install_identity_paging(&mut frames);
    if let Some(root) = page_tables {
        serial_write(b"Own page tables at 0x");
        serial_hex(root);
        serial_write(b"\r\n");
    } else {
        serial_write(b"Cannot allocate page tables\r\n");
    }
    let interrupt_table = install_timer(&mut frames);
    if interrupt_table.is_some() {
        serial_write(b"PIT timer interrupts enabled\r\n");
        while TICKS.load(Ordering::Relaxed) < 5 {
            core::hint::spin_loop();
        }
        serial_write(b"PIT timer ticks confirmed\r\n");
    }
    let mut user_runtime = match (page_tables, interrupt_table) {
        (Some(pml4), Some(idt)) => user::Runtime::install(&mut frames, pml4, idt),
        _ => None,
    };
    if let Some(runtime) = user_runtime.as_ref() {
        serial_write(b"Ring 3 AIS64 runtime installed\r\n");
        serial_write(b"AIS64 separate page tables at 0x");
        serial_hex(runtime.page_tables());
        serial_write(b"\r\n");
    }
    let mut probe = [0u8; 512];
    if !ata_read_sector_raw(0, &mut probe) && info.ramdisk_size >= 512 {
        USE_RAM_DISK.store(true, Ordering::Relaxed);
        serial_write(b"AOS using writable RAM disk from boot media\r\n");
    }
    let mut disk = read_root_info();
    if disk.is_some() {
        if USE_RAM_DISK.load(Ordering::Relaxed) {
            serial_write(b"AOS FAT12 volume found in RAM\r\n");
        } else {
            serial_write(b"AOS FAT12 volume found on IDE\r\n");
        }
    } else {
        serial_write(b"AOS FAT12 volume unavailable\r\n");
    }

    // QEMU/EDK2 keeps the linear framebuffer mapping live after the handoff.
    // A page-table manager must map it explicitly before replacing that map.
    render_desktop(info, disk);
    serial_write(b"Framebuffer painted by kernel\r\n");
    let mouse_ready = ps2_enable_mouse();
    if mouse_ready { serial_write(b"PS/2 mouse enabled\r\n"); }
    store_serial_init();
    let mut mouse = Mouse::new(info);
    if mouse_ready { mouse.draw(info); }
    let mut selected = 0usize;
    let mut active = 0usize;
    let mut file_selection = 0usize;
    let mut current_directory = 0u16;
    let mut directory_stack = [0u16; 8];
    let mut directory_depth = 0usize;
    let mut program_selection = 0usize;
    let mut opened_file: Option<FileEntry> = None;
    let mut file_buffer = [0u8; 4096];
    let mut note_buffer = [0u8; 4096];
    let mut note_length = disk.and_then(|root| find_file(root, b"NOTES   TXT")
        .map(|file| read_file(root, file, &mut note_buffer))).unwrap_or(0);
    let mut write_buffer = [0u8; 4096];
    let mut write_length = disk.and_then(|root| find_file(root, b"DOCUMENTTXT")
        .map(|file| read_file(root, file, &mut write_buffer))).unwrap_or(0);
    let mut write_cursor = write_length;
    let mut terminal_line = [0u8; 64];
    let mut terminal_length = 0usize;
    let mut terminal_output = [0u8; 2048];
    let mut terminal_output_length = 0usize;
    let mut program_output = [0u8; 256];
    let mut store_catalogue = [0u8; 42 * 16];
    let mut store_count = 0usize;
    let mut store_selection = 0usize;
    let mut store_status = 0u8;
    let mut ctrl = false;
    let mut shift = false;
    let mut notes_dirty = false;
    let mut write_dirty = false;
    let mut start_query = [0u8; 32];
    let mut start_query_length = 0usize;
    let mut start_selection = 0usize;
    let mut last_frame = TICKS.load(Ordering::Relaxed);
    let mut last_clock_check = last_frame;
    let mut clock_minute = read_clock().1;
    macro_rules! redraw_current_window {
        () => {{
            paint_wallpaper(info);
            render_window(info, active, disk, file_selection, current_directory);
            match active {
                0 => if let Some(file) = opened_file {
                    render_file_view(info, file,
                        &file_buffer[..(file.size as usize).min(file_buffer.len())]);
                },
                1 => render_notes(info, &note_buffer[..note_length], false),
                7 => render_write(info, &write_buffer[..write_length], write_cursor, false),
                5 => render_program_list(info, disk, program_selection),
                4 => render_store(info, &store_catalogue, store_count,
                                  store_selection, store_status),
                3 => render_terminal(info, &terminal_output[..terminal_output_length],
                                     &terminal_line[..terminal_length]),
                _ => {}
            }
            render_taskbar(info, selected);
        }};
    }
    macro_rules! redraw_shell {
        () => {{
            START_BACKING_VALID.store(false, Ordering::Relaxed);
            if WINDOW_VISIBLE.load(Ordering::Relaxed) {
                redraw_current_window!();
            } else {
                paint_wallpaper(info);
                render_taskbar(info, selected);
            }
        }};
    }
    macro_rules! activate_application {
        ($app:expr) => {{
            selected = $app;
            active = selected;
            opened_file = None;
            START_OPEN.store(false, Ordering::Relaxed);
            WINDOW_VISIBLE.store(true, Ordering::Relaxed);
            redraw_current_window!();
        }};
    }
    loop {
        let controller_status = inb(0x64);
        if controller_status & 1 != 0 {
            let scan = inb(0x60);
            if controller_status & 0x20 != 0 {
                if mouse_ready && mouse.packet_byte(scan, info) {
                    let taskbar_y = info.height - TASKBAR_BUFFER_HEIGHT;
                    if mouse.y >= taskbar_y && mouse.x < TASKBAR_APPS_X - 8 {
                        let open = mouse.x >= 56 || !START_OPEN.load(Ordering::Relaxed);
                        START_OPEN.store(open, Ordering::Relaxed);
                        start_query_length = 0;
                        start_selection = 0;
                        mouse.erase(info);
                        redraw_shell!();
                        if open { render_start_menu(info, &[], 0); }
                        mouse.draw(info);
                        serial_write(if open { b"AOS Start menu opened\r\n" }
                                     else { b"AOS Start menu closed\r\n" });
                        continue;
                    }
                    if START_OPEN.load(Ordering::Relaxed) {
                        let (sx, sy, sw, sh) = start_menu_rect(info);
                        let mut app = None;
                        if mouse.x >= sx && mouse.x < sx + sw
                            && mouse.y >= sy && mouse.y < sy + sh {
                            if mouse.x < sx + 306 && mouse.y >= sy + 112
                                && mouse.y < sy + 112 + 7 * 48 {
                                app = start_app_at(&start_query[..start_query_length],
                                                    (mouse.y - sy - 112) / 48);
                            } else if mouse.x >= sx + 330 && mouse.x < sx + 514
                                && mouse.y >= sy + 150 && mouse.y < sy + 352 {
                                let col = (mouse.x - sx - 330) / 96;
                                let row = (mouse.y - sy - 150) / 106;
                                if col < 2 && row < 2 { app = Some([7, 1, 0, 2][row * 2 + col]); }
                            } else if mouse.y >= sy + sh - 58 && mouse.x >= sx + sw - 160 {
                                app = Some(6);
                            }
                            if let Some(app) = app {
                                mouse.erase(info);
                                activate_application!(app);
                                mouse.draw(info);
                                serial_write(b"AOS Start launched: ");
                                serial_write(LABELS[app].as_bytes());
                                serial_write(b"\r\n");
                            }
                        } else {
                            START_OPEN.store(false, Ordering::Relaxed);
                            mouse.erase(info);
                            redraw_shell!();
                            mouse.draw(info);
                            serial_write(b"AOS Start menu dismissed\r\n");
                        }
                        continue;
                    }
                    let (wx, wy, ww, _) = window_rect(info);
                    let controls_x = wx + ww - 8 - 3 * WINDOW_BUTTON_WIDTH;
                    if WINDOW_VISIBLE.load(Ordering::Relaxed)
                        && mouse.y >= wy && mouse.y < wy + 48
                        && mouse.x >= controls_x && mouse.x < wx + ww - 8 {
                        let control = (mouse.x - controls_x) / WINDOW_BUTTON_WIDTH;
                        mouse.erase(info);
                        if control == 1 {
                            let maximized = !WINDOW_MAXIMIZED.load(Ordering::Relaxed);
                            WINDOW_MAXIMIZED.store(maximized, Ordering::Relaxed);
                            redraw_current_window!();
                            serial_write(if maximized { b"AOS window maximized\r\n" }
                                         else { b"AOS window restored\r\n" });
                        } else {
                            WINDOW_VISIBLE.store(false, Ordering::Relaxed);
                            if control == 2 { opened_file = None; }
                            paint_wallpaper(info);
                            render_taskbar(info, selected);
                            serial_write(if control == 0 { b"AOS window minimized\r\n" }
                                         else { b"AOS window closed\r\n" });
                        }
                        mouse.draw(info);
                        continue;
                    }
                    if mouse.y >= taskbar_y
                        && mouse.x >= TASKBAR_APPS_X && mouse.x < TASKBAR_APPS_X + 8 * TASKBAR_TILE
                    {
                        let tile = (mouse.x - TASKBAR_APPS_X) / TASKBAR_TILE;
                        mouse.erase(info);
                        if tile == active && WINDOW_VISIBLE.load(Ordering::Relaxed) {
                            WINDOW_VISIBLE.store(false, Ordering::Relaxed);
                            paint_wallpaper(info);
                            render_taskbar(info, selected);
                            serial_write(b"AOS taskbar minimized window\r\n");
                        } else {
                            activate_application!(tile);
                        }
                        mouse.draw(info);
                        serial_write(b"Mouse taskbar application: ");
                        serial_hex(active as u64);
                        serial_write(b"\r\n");
                    } else if WINDOW_VISIBLE.load(Ordering::Relaxed)
                        && active == 0 && opened_file.is_none() {
                        let (wx, wy, _, _) = window_rect(info);
                        if mouse.x >= wx + 32 && mouse.x < wx + 502
                            && mouse.y >= wy + 112 && mouse.y < wy + 112 + 12 * 24
                        {
                            let scroll = file_selection.saturating_sub(10);
                            file_selection = (scroll + (mouse.y - wy - 112) / 24)
                                .min(disk.map_or(0, |root| directory_count(root,
                                    current_directory).saturating_sub(1)));
                            mouse.erase(info);
                            render_file_listing(info, disk, file_selection, current_directory);
                            mouse.draw(info);
                        }
                    }
                }
                continue;
            }
            if scan == 0x1d { ctrl = true; }
            else if scan == 0x9d { ctrl = false; }
            else if scan == 0x2a || scan == 0x36 { shift = true; }
            else if scan == 0xaa || scan == 0xb6 { shift = false; }
            else if scan & 0x80 == 0 {
                if scan == 0x5b || scan == 0x5c || (ctrl && scan == 0x01) {
                    let open = !START_OPEN.load(Ordering::Relaxed);
                    START_OPEN.store(open, Ordering::Relaxed);
                    start_query_length = 0;
                    start_selection = 0;
                    mouse.erase(info);
                    redraw_shell!();
                    if open { render_start_menu(info, &[], 0); }
                    mouse.draw(info);
                    serial_write(if open { b"AOS Start menu opened\r\n" }
                                 else { b"AOS Start menu closed\r\n" });
                    continue;
                }
                if START_OPEN.load(Ordering::Relaxed) {
                    mouse.erase(info);
                    match scan {
                        0x01 => { START_OPEN.store(false, Ordering::Relaxed); redraw_shell!(); },
                        0x48 => start_selection = start_selection.saturating_sub(1),
                        0x50 => start_selection = (start_selection + 1).min(
                            start_app_count(&start_query[..start_query_length]).saturating_sub(1)),
                        0x0e => { start_query_length = start_query_length.saturating_sub(1); start_selection = 0; },
                        0x1c => {
                            if let Some(app) = start_app_at(&start_query[..start_query_length], start_selection) {
                                activate_application!(app);
                                serial_write(b"AOS Start launched: ");
                                serial_write(LABELS[app].as_bytes());
                                serial_write(b"\r\n");
                            }
                        }
                        _ => if let Some(byte) = scan_to_ascii(scan, shift) {
                            if start_query_length < start_query.len() {
                                start_query[start_query_length] = byte;
                                start_query_length += 1;
                                start_selection = 0;
                            }
                        }
                    }
                    if START_OPEN.load(Ordering::Relaxed) {
                        render_start_menu(info, &start_query[..start_query_length], start_selection);
                    }
                    mouse.draw(info);
                    continue;
                }
                if !WINDOW_VISIBLE.load(Ordering::Relaxed) {
                    match scan {
                        0x4b => selected = selected.saturating_sub(1),
                        0x4d => selected = (selected + 1).min(7),
                        0x1c | 0x01 => {
                            if scan == 0x01 { selected = 0; }
                            if selected != active { opened_file = None; }
                            active = selected;
                            mouse.erase(info);
                            redraw_current_window!();
                            mouse.draw(info);
                            serial_write(b"AOS window reopened\r\n");
                        }
                        _ => {}
                    }
                    continue;
                }
                if active == 1 && ctrl && scan == 0x1f {
                    let saved = disk.is_some_and(|root| save_text(root, b"NOTES   TXT",
                        &note_buffer[..note_length]));
                    if saved {
                        disk = read_root_info();
                        serial_write(b"AOS Notes saved to FAT12\r\n");
                    } else {
                        serial_write(b"AOS Notes save failed\r\n");
                    }
                    render_notes(info, &note_buffer[..note_length], saved);
                    continue;
                }
                if active == 7 && selected == 7 {
                    if ctrl && scan == 0x1f {
                        let saved = disk.is_some_and(|root| save_text(root, b"DOCUMENTTXT",
                            &write_buffer[..write_length]));
                        if saved {
                            disk = read_root_info();
                            serial_write(b"AOS Write saved DOCUMENT.TXT\r\n");
                        }
                        render_write(info, &write_buffer[..write_length], write_cursor, saved);
                        continue;
                    }
                    if ctrl && scan == 0x31 {
                        write_length = 0;
                        write_cursor = 0;
                        write_dirty = true;
                        continue;
                    }
                    if ctrl && scan == 0x18 {
                        write_length = disk.and_then(|root| find_file(root, b"DOCUMENTTXT")
                            .map(|file| read_file(root, file, &mut write_buffer))).unwrap_or(0);
                        write_cursor = write_length;
                        write_dirty = true;
                        continue;
                    }
                    if !ctrl {
                        match scan {
                            0x4b => write_cursor = write_cursor.saturating_sub(1),
                            0x4d => write_cursor = (write_cursor + 1).min(write_length),
                            0x47 => write_cursor = 0,
                            0x4f => write_cursor = write_length,
                            0x0e if write_cursor > 0 => {
                                write_buffer.copy_within(write_cursor..write_length,
                                                         write_cursor - 1);
                                write_cursor -= 1;
                                write_length -= 1;
                            }
                            0x53 if write_cursor < write_length => {
                                write_buffer.copy_within(write_cursor + 1..write_length,
                                                         write_cursor);
                                write_length -= 1;
                            }
                            _ => {
                                let letter = if scan == 0x1c { Some(b'\n') }
                                             else { scan_to_ascii(scan, shift) };
                                if let Some(letter) = letter {
                                    if write_length < write_buffer.len() {
                                        write_buffer.copy_within(write_cursor..write_length,
                                                                 write_cursor + 1);
                                        write_buffer[write_cursor] = letter;
                                        write_cursor += 1;
                                        write_length += 1;
                                    }
                                } else if scan != 0x01 { continue; }
                            }
                        }
                        if scan != 0x01 {
                            write_dirty = true;
                            continue;
                        }
                    }
                }
                if active == 1 && selected == 1 {
                    if scan == 0x0e {
                        note_length = note_length.saturating_sub(1);
                    } else if scan == 0x1c && note_length < note_buffer.len() {
                        note_buffer[note_length] = b'\n'; note_length += 1;
                    } else if let Some(letter) = scan_to_ascii(scan, shift) {
                        if note_length < note_buffer.len() {
                            note_buffer[note_length] = letter; note_length += 1;
                        }
                    }
                    if scan == 0x0e || scan == 0x1c || scan_to_ascii(scan, shift).is_some() {
                        notes_dirty = true;
                        continue;
                    }
                }
                if active == 3 && selected == 3 {
                    if scan == 0x0e {
                        terminal_length = terminal_length.saturating_sub(1);
                    } else if scan == 0x1c {
                        serial_write(b"AOS terminal command: ");
                        serial_write(&terminal_line[..terminal_length]);
                        serial_write(b"\r\n");
                        terminal_output_length = terminal_command(
                            disk, &terminal_line[..terminal_length], &mut terminal_output);
                        terminal_length = 0;
                    } else if let Some(letter) = scan_to_ascii(scan, shift) {
                        if terminal_length < terminal_line.len() {
                            terminal_line[terminal_length] = letter;
                            terminal_length += 1;
                        }
                    }
                    if scan == 0x0e || scan == 0x1c || scan_to_ascii(scan, shift).is_some() {
                        render_terminal(info, &terminal_output[..terminal_output_length],
                                        &terminal_line[..terminal_length]);
                        continue;
                    }
                }
                match scan {
                    0x4b => selected = selected.saturating_sub(1),
                    0x4d => selected = (selected + 1).min(7),
                    0x48 if active == 0 && opened_file.is_none() => {
                        file_selection = file_selection.saturating_sub(1);
                        render_file_listing(info, disk, file_selection, current_directory);
                    }
                    0x50 if active == 0 && opened_file.is_none() => {
                        let count = disk.map_or(0, |root| directory_count(root,
                            current_directory));
                        file_selection = (file_selection + 1).min(count.saturating_sub(1));
                        render_file_listing(info, disk, file_selection, current_directory);
                    }
                    0x48 if active == 5 => {
                        program_selection = program_selection.saturating_sub(1);
                        render_program_list(info, disk, program_selection);
                    }
                    0x50 if active == 5 => {
                        program_selection = (program_selection + 1).min(
                            disk.map_or(0, |root| native_program_count(root).saturating_sub(1)));
                        render_program_list(info, disk, program_selection);
                    }
                    0x48 if active == 4 => {
                        store_selection = store_selection.saturating_sub(1);
                        render_store(info, &store_catalogue, store_count,
                                     store_selection, store_status);
                    }
                    0x50 if active == 4 => {
                        store_selection = (store_selection + 1).min(store_count.saturating_sub(1));
                        render_store(info, &store_catalogue, store_count,
                                     store_selection, store_status);
                    }
                    0x1c => {
                        if selected != active {
                            active = selected;
                            opened_file = None;
                            render_window(info, active, disk, file_selection, current_directory);
                            if active == 1 {
                                render_notes(info, &note_buffer[..note_length], false);
                            } else if active == 7 {
                                render_write(info, &write_buffer[..write_length],
                                             write_cursor, false);
                            } else if active == 5 {
                                render_program_list(info, disk, program_selection);
                            } else if active == 4 {
                                render_store(info, &store_catalogue, store_count,
                                             store_selection, store_status);
                            } else if active == 3 {
                                render_terminal(info, &terminal_output[..terminal_output_length],
                                                &terminal_line[..terminal_length]);
                            }
                            serial_write(b"Taskbar application selected: ");
                            serial_hex(active as u64);
                            serial_write(b"\r\n");
                        } else if active == 0 {
                            if let Some(file) = disk.and_then(|root|
                                file_at_directory(root, current_directory, file_selection)) {
                                if file.attributes & 0x10 != 0 && file.first_cluster >= 2
                                    && directory_depth < directory_stack.len() {
                                    directory_stack[directory_depth] = current_directory;
                                    directory_depth += 1;
                                    current_directory = file.first_cluster;
                                    file_selection = 0;
                                    render_window(info, 0, disk, file_selection,
                                                  current_directory);
                                    serial_write(b"AOS folder opened\r\n");
                                } else if &file.name[8..11] == b"TXT" {
                                    let file_length = read_file(disk.unwrap(), file, &mut file_buffer);
                                    opened_file = Some(file);
                                    render_file_view(info, file, &file_buffer[..file_length]);
                                    serial_write(b"AOS text file opened: ");
                                    serial_write(&file.name);
                                    serial_write(b"\r\n");
                                }
                            }
                        } else if active == 6 {
                            serial_write(b"AOS power off requested\r\n");
                            outw(0x604, 0x2000);
                        } else if active == 5 {
                            let result = disk.and_then(|root| native_program_at(root, program_selection)
                                .and_then(|file| {
                                    serial_write(b"AIS64 launching: ");
                                    serial_write(&file.name);
                                    serial_write(b"\r\n");
                                    run_ais64(root, file, user_runtime.as_mut(),
                                              &mut program_output)
                                }));
                            if let Some(length) = result {
                                render_program_output(info, &program_output[..length]);
                                serial_write(b"AIS64 program executed: ");
                                serial_write(&program_output[..length]);
                                serial_write(b"\r\n");
                            } else {
                                serial_write(b"AIS64 program load failed or faulted\r\n");
                            }
                        } else if active == 4 {
                            serial_write(b"AOS Store Enter received\r\n");
                            if store_status != 1 {
                                serial_write(b"AOS Store requesting catalogue\r\n");
                                let length = store_fetch(b"L\n", &mut store_catalogue);
                                if let Some(length) = length.filter(|n| n % 42 == 0) {
                                    store_count = (length / 42).min(16);
                                    store_selection = 0;
                                    store_status = 1;
                                    serial_write(b"AOS Store catalogue received\r\n");
                                } else {
                                    store_status = 3;
                                    serial_write(b"AOS Store unavailable\r\n");
                                }
                            } else if store_count > 0 {
                                let record = &store_catalogue[store_selection * 42..][..42];
                                let mut request = [0u8; 11];
                                let mut filename = [b' '; 11];
                                request[0] = b'G';
                                let mut n = 0usize;
                                while n < 8 && record[n] != 0 {
                                    request[1 + n] = record[n];
                                    filename[n] = record[n];
                                    n += 1;
                                }
                                request[1 + n] = b'\n';
                                filename[8..11].copy_from_slice(b"AIS");
                                let mut program = [0u8; 4096];
                                let length = store_fetch(&request[..n + 2], &mut program);
                                let valid = length.is_some_and(|size| size >= 17
                                    && &program[..8] == AIS64_MAGIC);
                                let saved = valid && disk.is_some_and(|root| {
                                    save_text(root, &filename, &program[..length.unwrap()])
                                });
                                if saved {
                                    disk = read_root_info();
                                    store_status = 2;
                                    serial_write(b"AOS Store installed native AIS64 app\r\n");
                                } else {
                                    store_status = 3;
                                    serial_write(b"AOS Store install failed\r\n");
                                }
                            }
                            render_store(info, &store_catalogue, store_count,
                                         store_selection, store_status);
                        }
                    }
                    0x01 => {
                        if opened_file.take().is_some() {
                            render_window(info, 0, disk, file_selection, current_directory);
                        } else if active == 0 && directory_depth > 0 {
                            directory_depth -= 1;
                            current_directory = directory_stack[directory_depth];
                            file_selection = 0;
                            render_window(info, 0, disk, file_selection, current_directory);
                        } else {
                            active = 0;
                            selected = 0;
                            render_window(info, active, disk, file_selection, current_directory);
                        }
                    }
                    _ => {}
                }
            }
        }
        let now = TICKS.load(Ordering::Relaxed);
        if now.wrapping_sub(last_frame) >= 2 {
            last_frame = now;
            mouse.erase(info);
            if notes_dirty && active == 1 && WINDOW_VISIBLE.load(Ordering::Relaxed)
                && !START_OPEN.load(Ordering::Relaxed)
                && now % 10 < 2 {
                render_notes(info, &note_buffer[..note_length], false);
                notes_dirty = false;
            }
            if write_dirty && active == 7 && WINDOW_VISIBLE.load(Ordering::Relaxed)
                && !START_OPEN.load(Ordering::Relaxed)
                && now % 10 < 2 {
                render_write(info, &write_buffer[..write_length], write_cursor, false);
                write_dirty = false;
            }
            if TASKBAR_FOCUS.load(Ordering::Relaxed) != (selected * TASKBAR_TILE) as u64 {
                render_taskbar(info, selected);
            }
            if now.wrapping_sub(last_clock_check) >= 100 {
                last_clock_check = now;
                let minute = read_clock().1;
                if minute != clock_minute {
                    clock_minute = minute;
                    render_taskbar(info, selected);
                }
            }
            if mouse_ready { mouse.draw(info); }
        }
        core::hint::spin_loop();
    }
}

#[derive(Clone, Copy)]
struct RootInfo {
    fat_lba: u32,
    fat_size: u32,
    fat_count: u32,
    lba: u32,
    sectors: u32,
    data_lba: u32,
    volume_end_lba: u32,
    sectors_per_cluster: u32,
    entries: usize,
}

#[derive(Clone, Copy)]
struct FileEntry {
    name: [u8; 11],
    attributes: u8,
    first_cluster: u16,
    size: u32,
}

const AIS64_MAGIC: &[u8; 8] = b"AIS64\0\x01\0";

fn run_ais64(root: RootInfo, file: FileEntry, runtime: Option<&mut user::Runtime>,
             output: &mut [u8; 256]) -> Option<usize> {
    if file.size < 17 || file.size > 4096 { return None; }
    let mut image = [0u8; 4096];
    let count = read_file(root, file, &mut image);
    if count != file.size as usize || &image[..8] != AIS64_MAGIC { return None; }
    let length = u32::from_le_bytes(image[8..12].try_into().ok()?) as usize;
    let entry = u32::from_le_bytes(image[12..16].try_into().ok()?) as usize;
    if length == 0 || length > count - 16 || entry >= length { return None; }
    runtime?.run(&image[16..16 + length], entry, output)
}

fn inw(port: u16) -> u16 {
    let value: u16;
    unsafe { asm!("in ax, dx", in("dx") port, out("ax") value) }
    value
}

fn outw(port: u16, value: u16) {
    unsafe { asm!("out dx, ax", in("dx") port, in("ax") value) }
}

fn ata_read_sector(lba: u32, buffer: &mut [u8; 512]) -> bool {
    if USE_RAM_DISK.load(Ordering::Relaxed) {
        let offset = lba as usize * 512;
        let pointer = RAM_DISK_PTR.load(Ordering::Relaxed);
        if pointer == 0 || offset + 512 > RAM_DISK_SIZE.load(Ordering::Relaxed) {
            return false;
        }
        unsafe { core::ptr::copy_nonoverlapping((pointer + offset) as *const u8,
                                                buffer.as_mut_ptr(), 512); }
        return true;
    }
    ata_read_sector_raw(lba, buffer)
}

fn ata_read_sector_raw(lba: u32, buffer: &mut [u8; 512]) -> bool {
    if lba > 0x0fff_ffff {
        return false;
    }
    outb(0x1f6, 0xe0 | ((lba >> 24) as u8 & 0x0f));
    outb(0x1f2, 1);
    outb(0x1f3, lba as u8);
    outb(0x1f4, (lba >> 8) as u8);
    outb(0x1f5, (lba >> 16) as u8);
    outb(0x1f7, 0x20);
    let mut ready = false;
    for _ in 0..1_000_000 {
        let status = inb(0x1f7);
        if status & 0x21 != 0 {
            return false;
        }
        if status & 0x88 == 0x08 {
            ready = true;
            break;
        }
    }
    if !ready {
        return false;
    }
    for i in 0..256 {
        let word = inw(0x1f0);
        buffer[2 * i] = word as u8;
        buffer[2 * i + 1] = (word >> 8) as u8;
    }
    true
}

fn ata_write_sector(lba: u32, buffer: &[u8; 512]) -> bool {
    if USE_RAM_DISK.load(Ordering::Relaxed) {
        let offset = lba as usize * 512;
        let pointer = RAM_DISK_PTR.load(Ordering::Relaxed);
        if pointer == 0 || offset + 512 > RAM_DISK_SIZE.load(Ordering::Relaxed) {
            return false;
        }
        unsafe { core::ptr::copy_nonoverlapping(buffer.as_ptr(),
                                                (pointer + offset) as *mut u8, 512); }
        return true;
    }
    ata_write_sector_raw(lba, buffer)
}

fn ata_write_sector_raw(lba: u32, buffer: &[u8; 512]) -> bool {
    if lba > 0x0fff_ffff { return false; }
    outb(0x1f6, 0xe0 | ((lba >> 24) as u8 & 0x0f));
    outb(0x1f2, 1);
    outb(0x1f3, lba as u8);
    outb(0x1f4, (lba >> 8) as u8);
    outb(0x1f5, (lba >> 16) as u8);
    outb(0x1f7, 0x30);
    let mut ready = false;
    for _ in 0..1_000_000 {
        let status = inb(0x1f7);
        if status & 0x21 != 0 { return false; }
        if status & 0x88 == 0x08 { ready = true; break; }
    }
    if !ready { return false; }
    for i in 0..256 {
        outw(0x1f0, u16::from_le_bytes([buffer[2 * i], buffer[2 * i + 1]]));
    }
    outb(0x1f7, 0xe7);                 // flush ATA write cache
    for _ in 0..1_000_000 {
        let status = inb(0x1f7);
        if status & 0x80 == 0 { return status & 0x21 == 0; }
    }
    false
}

fn read_root_info() -> Option<RootInfo> {
    let mut boot = [0u8; 512];
    if !ata_read_sector(0, &mut boot) || boot[510] != 0x55 || boot[511] != 0xaa {
        return None;
    }
    let mut volume_lba = 0u32;
    if u16::from_le_bytes([boot[11], boot[12]]) != 512 {
        for slot in 0..4 {
            let at = 446 + slot * 16;
            if boot[at + 4] == 0x01 {
                volume_lba = u32::from_le_bytes(
                    [boot[at + 8], boot[at + 9], boot[at + 10], boot[at + 11]]);
                break;
            }
        }
        if volume_lba == 0 || !ata_read_sector(volume_lba, &mut boot) { return None; }
    }
    let sector_size = u16::from_le_bytes([boot[11], boot[12]]);
    if sector_size != 512 || boot[16] == 0 {
        return None;
    }
    let reserved = u16::from_le_bytes([boot[14], boot[15]]) as u32;
    let fat_size = u16::from_le_bytes([boot[22], boot[23]]) as u32;
    let entries = u16::from_le_bytes([boot[17], boot[18]]) as u32;
    let root_lba = volume_lba + reserved + boot[16] as u32 * fat_size;
    let root_sectors = (entries * 32).div_ceil(512);
    let total_sectors = u16::from_le_bytes([boot[19], boot[20]]) as u32;
    let mut count = 0usize;
    for sector in 0..root_sectors {
        let mut data = [0u8; 512];
        if !ata_read_sector(root_lba + sector, &mut data) {
            return None;
        }
        for slot in 0..16 {
            let entry = &data[slot * 32..slot * 32 + 32];
            if entry[0] == 0 { break; }
            if entry[0] != 0xe5 && entry[11] != 0x0f && entry[11] & 0x08 == 0 {
                count += 1;
            }
        }
    }
    Some(RootInfo {
        fat_lba: volume_lba + reserved,
        fat_size,
        fat_count: boot[16] as u32,
        lba: root_lba,
        sectors: root_sectors,
        data_lba: root_lba + root_sectors,
        volume_end_lba: volume_lba + total_sectors,
        sectors_per_cluster: boot[13] as u32,
        entries: count,
    })
}

fn file_at(root: RootInfo, wanted: usize) -> Option<FileEntry> {
    let mut index = 0usize;
    for sector in 0..root.sectors {
        let mut data = [0u8; 512];
        if !ata_read_sector(root.lba + sector, &mut data) { return None; }
        for slot in 0..16 {
            let entry = &data[slot * 32..slot * 32 + 32];
            if entry[0] == 0 { return None; }
            if entry[0] == 0xe5 || entry[11] == 0x0f || entry[11] & 0x08 != 0 { continue; }
            if index == wanted {
                let mut name = [0u8; 11];
                name.copy_from_slice(&entry[..11]);
                return Some(FileEntry {
                    name,
                    attributes: entry[11],
                    first_cluster: u16::from_le_bytes([entry[26], entry[27]]),
                    size: u32::from_le_bytes([entry[28], entry[29], entry[30], entry[31]]),
                });
            }
            index += 1;
        }
    }
    None
}

fn file_at_directory(root: RootInfo, directory: u16, wanted: usize) -> Option<FileEntry> {
    if directory == 0 { return file_at(root, wanted); }
    let mut cluster = directory;
    let mut index = 0usize;
    let limit = (root.volume_end_lba - root.data_lba) / root.sectors_per_cluster;
    for _ in 0..limit {
        if !(2..0xff8).contains(&cluster) { return None; }
        for sector in 0..root.sectors_per_cluster {
            let mut data = [0u8; 512];
            let lba = root.data_lba + (cluster as u32 - 2) * root.sectors_per_cluster + sector;
            if !ata_read_sector(lba, &mut data) { return None; }
            for slot in 0..16 {
                let entry = &data[slot * 32..slot * 32 + 32];
                if entry[0] == 0 { return None; }
                if entry[0] == b'.' || entry[0] == 0xe5 ||
                    entry[11] == 0x0f || entry[11] & 0x08 != 0 { continue; }
                if index == wanted {
                    let mut name = [0u8; 11];
                    name.copy_from_slice(&entry[..11]);
                    return Some(FileEntry {
                        name,
                        attributes: entry[11],
                        first_cluster: u16::from_le_bytes([entry[26], entry[27]]),
                        size: u32::from_le_bytes([entry[28], entry[29], entry[30], entry[31]]),
                    });
                }
                index += 1;
            }
        }
        cluster = fat12_next(root, cluster)?;
    }
    None
}

fn directory_count(root: RootInfo, directory: u16) -> usize {
    if directory == 0 { return root.entries; }
    let mut count = 0usize;
    while file_at_directory(root, directory, count).is_some() { count += 1; }
    count
}

fn find_file(root: RootInfo, name: &[u8; 11]) -> Option<FileEntry> {
    for index in 0..root.entries {
        let file = file_at(root, index)?;
        if &file.name == name { return Some(file); }
    }
    None
}

fn native_program_at(root: RootInfo, wanted: usize) -> Option<FileEntry> {
    let mut found = 0usize;
    for index in 0..root.entries {
        let file = file_at(root, index)?;
        if &file.name[8..11] != b"AIS" || file.size < 17 || file.size > 4096 {
            continue;
        }
        let mut header = [0u8; 8];
        if read_file(root, file, &mut header) != 8 || &header != AIS64_MAGIC {
            continue;
        }
        if found == wanted { return Some(file); }
        found += 1;
    }
    None
}

fn native_program_count(root: RootInfo) -> usize {
    let mut count = 0;
    while native_program_at(root, count).is_some() { count += 1; }
    count
}

fn fat12_next(root: RootInfo, cluster: u16) -> Option<u16> {
    let offset = cluster as u32 * 3 / 2;
    let mut data = [0u8; 512];
    if !ata_read_sector(root.fat_lba + offset / 512, &mut data) { return None; }
    let pos = (offset % 512) as usize;
    let low = data[pos];
    let high = if pos == 511 {
        if !ata_read_sector(root.fat_lba + offset / 512 + 1, &mut data) { return None; }
        data[0]
    } else { data[pos + 1] };
    let pair = u16::from_le_bytes([low, high]);
    Some(if cluster & 1 == 0 { pair & 0x0fff } else { pair >> 4 })
}

fn read_file(root: RootInfo, file: FileEntry, output: &mut [u8]) -> usize {
    let mut cluster = file.first_cluster;
    let mut total = 0usize;
    let limit = (file.size as usize).min(output.len());
    while total < limit && (2..0xff8).contains(&cluster) {
        for sector in 0..root.sectors_per_cluster {
            if total >= limit { break; }
            let mut data = [0u8; 512];
            let lba = root.data_lba + (cluster as u32 - 2) * root.sectors_per_cluster + sector;
            if !ata_read_sector(lba, &mut data) { return total; }
            let n = (limit - total).min(512);
            output[total..total + n].copy_from_slice(&data[..n]);
            total += n;
        }
        cluster = match fat12_next(root, cluster) { Some(next) => next, None => break };
    }
    total
}

fn fat12_set(root: RootInfo, cluster: u16, value: u16) -> bool {
    let offset = cluster as u32 * 3 / 2;
    let sector = offset / 512;
    let pos = (offset % 512) as usize;
    for copy in 0..root.fat_count {
        let lba = root.fat_lba + copy * root.fat_size + sector;
        let mut data = [0u8; 512];
        if !ata_read_sector(lba, &mut data) { return false; }
        let mut next = [0u8; 512];
        if pos == 511 && !ata_read_sector(lba + 1, &mut next) { return false; }
        let high = if pos == 511 { next[0] } else { data[pos + 1] };
        if cluster & 1 == 0 {
            data[pos] = value as u8;
            let updated = (high & 0xf0) | ((value >> 8) as u8 & 0x0f);
            if pos == 511 { next[0] = updated; } else { data[pos + 1] = updated; }
        } else {
            data[pos] = (data[pos] & 0x0f) | ((value << 4) as u8 & 0xf0);
            if pos == 511 { next[0] = (value >> 4) as u8; }
            else { data[pos + 1] = (value >> 4) as u8; }
        }
        if !ata_write_sector(lba, &data) { return false; }
        if pos == 511 && !ata_write_sector(lba + 1, &next) { return false; }
    }
    true
}

fn save_text(root: RootInfo, name: &[u8; 11], content: &[u8]) -> bool {
    if content.len() > 4096 || root.sectors_per_cluster != 1 { return false; }
    let existing = find_file(root, name);
    let required = content.len().max(1).div_ceil(512);
    let clusters = (root.volume_end_lba - root.data_lba) / root.sectors_per_cluster;
    let mut chain = [0u16; 8];
    let mut allocated = 0usize;
    for candidate in 2..clusters + 2 {
        if allocated == required { break; }
        if fat12_next(root, candidate as u16) == Some(0) {
            chain[allocated] = candidate as u16;
            allocated += 1;
        }
    }
    if allocated != required { return false; }
    for index in 0..required {
        let mut data = [0u8; 512];
        let start = index * 512;
        let end = content.len().min(start + 512);
        if end > start { data[..end - start].copy_from_slice(&content[start..end]); }
        let lba = root.data_lba + (chain[index] as u32 - 2) * root.sectors_per_cluster;
        if !ata_write_sector(lba, &data) { return false; }
    }
    for index in 0..required {
        let next = if index + 1 == required { 0x0fff } else { chain[index + 1] };
        if !fat12_set(root, chain[index], next) { return false; }
    }
    for sector in 0..root.sectors {
        let lba = root.lba + sector;
        let mut directory = [0u8; 512];
        if !ata_read_sector(lba, &mut directory) { return false; }
        for slot in 0..16 {
            let at = slot * 32;
            let entry = &mut directory[at..at + 32];
            if &entry[..11] == name || (existing.is_none() && (entry[0] == 0 || entry[0] == 0xe5)) {
                entry[..11].copy_from_slice(name);
                entry[11] = 0x20;
                entry[26..28].copy_from_slice(&chain[0].to_le_bytes());
                entry[28..32].copy_from_slice(&(content.len() as u32).to_le_bytes());
                if !ata_write_sector(lba, &directory) { return false; }
                if let Some(old) = existing {
                    let mut current = old.first_cluster;
                    for _ in 0..clusters {
                        if !(2..0xff8).contains(&current) { break; }
                        let next = fat12_next(root, current).unwrap_or(0x0fff);
                        if !fat12_set(root, current, 0) { return false; }
                        current = next;
                    }
                }
                return true;
            }
        }
    }
    false
}

struct FrameAllocator<'a> {
    info: &'a BootInfo,
    descriptor: usize,
    next: u64,
    remaining: u64,
}

impl<'a> FrameAllocator<'a> {
    fn new(info: &'a BootInfo) -> Self {
        Self { info, descriptor: 0, next: 0, remaining: 0 }
    }

    fn allocate(&mut self) -> Option<u64> {
        loop {
            if self.remaining != 0 {
                let page = self.next;
                self.next += 4096;
                self.remaining -= 1;
                unsafe { write_bytes(page as *mut u8, 0, 4096) };
                return Some(page);
            }
            let offset = self.descriptor.checked_mul(self.info.descriptor_size)?;
            if offset.checked_add(40)? > self.info.memory_map_size {
                return None;
            }
            self.descriptor += 1;
            let base = unsafe { self.info.memory_map.add(offset) };
            let kind = unsafe { read_unaligned(base as *const u32) };
            let address = unsafe { read_unaligned(base.add(8) as *const u64) };
            let pages = unsafe { read_unaligned(base.add(24) as *const u64) };
            if kind == 7 && address >= 0x100000 && address < 0x1_0000_0000 {
                self.next = address;
                self.remaining = pages.min((0x1_0000_0000 - address) / 4096);
            }
        }
    }
}

fn install_identity_paging(frames: &mut FrameAllocator<'_>) -> Option<u64> {
    let pml4 = frames.allocate()?;
    let pdpt = frames.allocate()?;
    unsafe { ((pml4 as *mut u64).add(0)).write(pdpt | 0x3) };
    for gb in 0..4u64 {
        let directory = frames.allocate()?;
        unsafe { ((pdpt as *mut u64).add(gb as usize)).write(directory | 0x3) };
        for page in 0..512u64 {
            let physical = (gb * 512 + page) * 0x200000;
            let cache_disable = if physical >= 0xe0000000 { 0x10 } else { 0 };
            unsafe {
                ((directory as *mut u64).add(page as usize))
                    .write(physical | 0x83 | cache_disable);
            }
        }
    }
    unsafe { asm!("mov cr3, {}", in(reg) pml4, options(nostack, preserves_flags)) };
    Some(pml4)
}

fn install_timer(frames: &mut FrameAllocator<'_>) -> Option<u64> {
    let table = frames.allocate()?;
    let handler = aos_timer_isr as *const () as usize as u64;
    let selector: u16;
    unsafe { asm!("mov {0:x}, cs", out(reg) selector, options(nomem, nostack)) };
    let gate = IdtEntry {
        offset_low: handler as u16,
        selector,
        ist: 0,
        flags: 0x8e,
        offset_mid: (handler >> 16) as u16,
        offset_high: (handler >> 32) as u32,
        reserved: 0,
    };
    unsafe { ((table as *mut IdtEntry).add(0x20)).write(gate) };
    let pointer = IdtPointer { limit: 4095, base: table };
    unsafe { asm!("lidt [{}]", in(reg) &pointer, options(readonly, nostack)) };

    // Remap the legacy PIC to vectors 0x20..0x2f and unmask IRQ0 only.
    outb(0x20, 0x11);
    outb(0xa0, 0x11);
    outb(0x21, 0x20);
    outb(0xa1, 0x28);
    outb(0x21, 0x04);
    outb(0xa1, 0x02);
    outb(0x21, 0x01);
    outb(0xa1, 0x01);
    outb(0x21, 0xfe);
    outb(0xa1, 0xff);
    outb(0x43, 0x36);
    let divisor = 11932u16;             // about 100 PIT ticks per second
    outb(0x40, divisor as u8);
    outb(0x40, (divisor >> 8) as u8);
    unsafe { asm!("sti", options(nomem, nostack)) };
    Some(table)
}

const LABELS: [&str; 8] = ["Files", "Notes", "System", "Terminal", "Store", "Programs", "Power", "Write"];
const APP_COLOURS: [[u8; 3]; 8] = [
    [71, 148, 244], [245, 178, 75], [105, 145, 218], [38, 53, 86],
    [74, 185, 207], [132, 115, 226], [224, 99, 118], [78, 122, 230],
];

fn render_desktop(info: &BootInfo, disk: Option<RootInfo>) {
    paint_wallpaper(info);
    render_window(info, 0, disk, 0, 0);
    render_taskbar(info, 0);
}

fn paint_wallpaper(info: &BootInfo) {
    paint_wallpaper_region(info, 0, 0, info.width, info.height);
    render_top_bar(info);
}

fn render_top_bar(info: &BootInfo) {
    fill_rect(info, 0, 0, info.width, 44, [235, 242, 255]);
    fill_rect(info, 0, 43, info.width, 1, [215, 226, 247]);
    draw_text(info, 20, 4, "AOS", 2, [219, 54, 76]);
    fill_rect(info, 80, 12, 1, 20, [194, 208, 232]);
    let label = if WINDOW_VISIBLE.load(Ordering::Relaxed) {
        LABELS[SHELL_ACTIVE.load(Ordering::Relaxed)]
    } else { "Desktop" };
    draw_text(info, 101, 4, label, 2, [46, 64, 100]);
}

fn wallpaper_colour(w: i64, h: i64, x: i64, y: i64) -> [u8; 3] {
    let mut rgb = [51 + 22 * y / h, 105 + 31 * x / w, 208 + 20 * y / h];
    for (cx, cy, radius, colour) in [
        (w / 7, h * 4 / 5, w * 3 / 5, [114, 70, 210]),
        (w * 9 / 10, h * 3 / 4, w * 2 / 3, [75, 204, 244]),
    ] {
        let dx = x - cx;
        let dy = y - cy;
        let distance = dx * dx + dy * dy;
        let radius2 = radius * radius;
        if distance < radius2 {
            let alpha = (radius2 - distance) * 190 / radius2;
            for channel in 0..3 {
                rgb[channel] = (rgb[channel] * (255 - alpha) + colour[channel] * alpha) / 255;
            }
        }
    }
    [rgb[0] as u8, rgb[1] as u8, rgb[2] as u8]
}

fn paint_wallpaper_region(info: &BootInfo, x: usize, y: usize,
                          width: usize, height: usize) {
    let w = info.width as i64;
    let h = info.height as i64;
    if w == 0 || h == 0 { return; }
    for py in y..y.saturating_add(height).min(info.height) {
        for px in x..x.saturating_add(width).min(info.width) {
            put_pixel(info, px, py, wallpaper_colour(w, h, px as i64, py as i64));
        }
    }
}

fn window_rect(info: &BootInfo) -> (usize, usize, usize, usize) {
    if WINDOW_MAXIMIZED.load(Ordering::Relaxed) {
        return (0, 44, info.width, info.height - TASKBAR_BUFFER_HEIGHT - 44);
    }
    let width = (info.width * 3 / 5).min(780);
    let height = (info.height * 3 / 5).min(480);
    ((info.width - width) / 2, (info.height - height) / 2 - 12, width, height)
}

fn render_window(info: &BootInfo, active: usize, disk: Option<RootInfo>,
                 selection: usize, directory: u16) {
    WINDOW_VISIBLE.store(true, Ordering::Relaxed);
    SHELL_ACTIVE.store(active, Ordering::Relaxed);
    let (x, y, width, height) = window_rect(info);
    paint_wallpaper_region(info, x.saturating_sub(20), y.saturating_sub(20),
                           width + 40, height + 42);
    let radius = if WINDOW_MAXIMIZED.load(Ordering::Relaxed) { 0 } else { 18 };
    rounded_rect(info, x.saturating_sub(16), y.saturating_sub(8), width + 32, height + 28,
                 28, [15, 28, 71], 24);
    rounded_rect(info, x.saturating_sub(7), y.saturating_sub(3), width + 14, height + 15,
                 23, [16, 33, 75], 25);
    rounded_rect(info, x, y, width, height, radius, [248, 250, 255], 255);
    rounded_rect(info, x + 1, y + 1, width - 2, 48,
                 radius, [235, 242, 252], 255);
    fill_rect(info, x + 1, y + 19, width - 2, 30, [235, 242, 252]);
    fill_rect(info, x + 1, y + 49, width - 2, 1, [221, 229, 241]);
    draw_text(info, x + 24, y + 9, LABELS[active], 2, [31, 48, 83]);
    render_window_controls(info, x + width - 8 - 3 * WINDOW_BUTTON_WIDTH, y);
    if active == 0 {
        draw_file_list(info, x + 32, y + 80, disk, selection, directory);
    } else if active == 2 {
        draw_text(info, x + 32, y + 88, "AOS SYSTEM", 2, [28, 52, 105]);
        draw_text(info, x + 32, y + 132, "CPU: x86-64 long mode", 2, [49, 68, 105]);
        draw_text(info, x + 32, y + 162, "Graphics: linear 32-bit framebuffer", 2, [49, 68, 105]);
        draw_text(info, x + 32, y + 192, "Resolution (hex):", 2, [49, 68, 105]);
        draw_hex_u32(info, x + 320, y + 192, info.width as u32);
        draw_text(info, x + 32, y + 222, "Storage: ATA PIO / FAT12", 2, [49, 68, 105]);
        draw_text(info, x + 32, y + 252, "Timer: PIT at about 100 Hz", 2, [49, 68, 105]);
    } else if active == 6 {
        draw_text(info, x + 32, y + 88, "SHUT DOWN AOS", 2, [28, 52, 105]);
        draw_text(info, x + 32, y + 130, "Press Enter again to power off.", 2, [49, 68, 105]);
        draw_text(info, x + 32, y + 158, "Press Esc to return to Files.", 2, [49, 68, 105]);
    } else if active == 5 {
        draw_text(info, x + 32, y + 88, "NATIVE 64-BIT PROGRAMS", 2, [28, 52, 105]);
        draw_text(info, x + 32, y + 132, "Up/Down: select   Enter: run", 2, [49, 68, 105]);
    } else if active == 7 {
        draw_text(info, x + 32, y + 88, "DOCUMENT.TXT", 2, [28, 52, 105]);
    } else {
        draw_text(info, x + 32, y + 88, "AOS - 64-bit kernel preview", 2, [28, 52, 105]);
        draw_text(info, x + 32, y + 130, "UEFI boot services have exited.", 2, [49, 68, 105]);
        draw_text(info, x + 32, y + 158, "Framebuffer and input run in the kernel.", 2, [49, 68, 105]);
    }
    draw_text(info, x + 32, y + height - 58, "Windows key: Start   Left/Right: apps   Enter: open", 1, [65, 87, 125]);
    render_taskbar(info, active);
    render_top_bar(info);
}

fn render_program_list(info: &BootInfo, disk: Option<RootInfo>, selected: usize) {
    let (x, y, width, _) = window_rect(info);
    fill_rect(info, x + 16, y + 168, width - 32, 210, [245, 248, 255]);
    let Some(root) = disk else { return };
    for index in 0..8 {
        let Some(file) = native_program_at(root, index) else { break };
        if index == selected {
            fill_rect(info, x + 26, y + 174 + index * 24, width - 52, 22,
                      [215, 232, 253]);
        }
        let mut filename = [0u8; 12];
        filename[..8].copy_from_slice(&file.name[..8]);
        filename[8] = b'.';
        filename[9..12].copy_from_slice(&file.name[8..11]);
        if let Ok(name) = core::str::from_utf8(&filename) {
            draw_text(info, x + 40, y + 178 + index * 24, name, 2, [37, 56, 98]);
        }
    }
}

fn render_program_output(info: &BootInfo, output: &[u8]) {
    let (x, y, width, _) = window_rect(info);
    fill_rect(info, x + 16, y + 176, width - 32, 100, [231, 240, 255]);
    if let Ok(text) = core::str::from_utf8(output) {
        draw_text(info, x + 32, y + 196, text, 2, [24, 48, 100]);
    }
}

fn draw_file_list(info: &BootInfo, x: usize, y: usize,
                  disk: Option<RootInfo>, selection: usize, directory: u16) {
    draw_text(info, x, y, if directory == 0 { "AOS FAT12 FILES" }
              else { "FOLDER - ESC GOES BACK" }, 2, [28, 52, 105]);
    let Some(root) = disk else {
        draw_text(info, x, y + 36, "No IDE volume found", 2, [135, 46, 65]);
        return;
    };
    let first = selection.saturating_sub(10);
    for row in 0..12 {
        let index = first + row;
        let Some(file) = file_at_directory(root, directory, index) else { break };
        let mut filename = [0u8; 13];
        let mut len = 0usize;
        for &letter in &file.name[..8] {
            if letter != b' ' { filename[len] = letter; len += 1; }
        }
        if file.name[8] != b' ' {
            filename[len] = b'.'; len += 1;
            for &letter in &file.name[8..11] {
                if letter != b' ' { filename[len] = letter; len += 1; }
            }
        }
        let name = core::str::from_utf8(&filename[..len]).unwrap_or("FILE");
        if index == selection {
            fill_rect(info, x, y + 32 + row * 24, 470, 23, [215, 232, 253]);
        }
        draw_text(info, x + 8, y + 36 + row * 24, name, 2, [37, 56, 98]);
        if file.attributes & 0x10 != 0 {
            draw_text(info, x + 296, y + 36 + row * 24, "<DIR>", 1, [100, 119, 153]);
        } else {
            draw_hex_u32(info, x + 296, y + 36 + row * 24, file.size);
        }
    }
}

fn render_file_listing(info: &BootInfo, disk: Option<RootInfo>,
                       selection: usize, directory: u16) {
    let (x, y, width, height) = window_rect(info);
    fill_rect(info, x + 16, y + 64, width - 32, height - 120, [245, 248, 255]);
    draw_file_list(info, x + 32, y + 80, disk, selection, directory);
}

fn render_file_view(info: &BootInfo, file: FileEntry, data: &[u8]) {
    let (x, y, width, height) = window_rect(info);
    fill_rect(info, x + 16, y + 64, width - 32, height - 120, [245, 248, 255]);
    let mut name = [0u8; 13];
    let mut len = 0usize;
    for &letter in &file.name[..8] {
        if letter != b' ' { name[len] = letter; len += 1; }
    }
    name[len] = b'.'; len += 1;
    for &letter in &file.name[8..11] {
        if letter != b' ' { name[len] = letter; len += 1; }
    }
    draw_text(info, x + 32, y + 72,
              core::str::from_utf8(&name[..len]).unwrap_or("TEXT"), 2, [28, 52, 105]);
    let mut column_px = 0usize;
    let mut row = 0usize;
    for &byte in data {
        if byte == b'\r' { continue; }
        if byte == b'\n' { column_px = 0; row += 1; continue; }
        if row >= 20 { break; }
        if column_px >= width - 80 { column_px = 0; row += 1; }
        if (32..=126).contains(&byte) {
            let letter = [byte];
            if let Ok(text) = core::str::from_utf8(&letter) {
                draw_text(info, x + 32 + column_px, y + 110 + row * 16,
                          text, 1, [37, 56, 98]);
            }
            column_px += char_advance(byte, 1);
        }
    }
}

fn draw_hex_u32(info: &BootInfo, x: usize, y: usize, number: u32) {
    let mut digits = [b'0'; 8];
    for i in 0..8 {
        let value = ((number >> ((7 - i) * 4)) & 15) as u8;
        digits[i] = if value < 10 { b'0' + value } else { b'A' + value - 10 };
    }
    if let Ok(text) = core::str::from_utf8(&digits) {
        draw_text(info, x, y, text, 2, [100, 119, 153]);
    }
}

fn render_notes(info: &BootInfo, text: &[u8], saved: bool) {
    let (x, y, width, height) = window_rect(info);
    fill_rect(info, x + 16, y + 64, width - 32, height - 120, [245, 248, 255]);
    draw_text(info, x + 32, y + 72, "NOTES.TXT", 2, [28, 52, 105]);
    let mut column_px = 0usize;
    let mut row = 0usize;
    for &byte in text {
        if byte == b'\n' { column_px = 0; row += 1; continue; }
        if row >= 20 { break; }
        if column_px >= width - 80 { column_px = 0; row += 1; }
        let letter = [byte];
        if let Ok(value) = core::str::from_utf8(&letter) {
            draw_text(info, x + 32 + column_px, y + 110 + row * 16,
                      value, 1, [37, 56, 98]);
        }
        column_px += char_advance(byte, 1);
    }
    fill_rect(info, x + 16, y + height - 60, width - 32, 28, [245, 248, 255]);
    draw_text(info, x + 32, y + height - 54,
              if saved { "Saved to NOTES.TXT" } else { "Type to edit. Ctrl+S saves." },
              1, [65, 87, 125]);
}

fn render_write(info: &BootInfo, text: &[u8], cursor: usize, saved: bool) {
    let (x, y, width, height) = window_rect(info);
    fill_rect(info, x + 16, y + 64, width - 32, height - 120, [237, 242, 251]);
    fill_rect(info, x + 54, y + 68, width - 108, height - 128, [255, 255, 255]);
    draw_text(info, x + 76, y + 82, "DOCUMENT.TXT", 2, [28, 52, 105]);
    let mut column_px = 0usize;
    let mut row = 0usize;
    for (index, &byte) in text.iter().enumerate() {
        if index == cursor && row < 19 {
            fill_rect(info, x + 76 + column_px, y + 124 + row * 16,
                      2, 12, [55, 120, 221]);
        }
        if byte == b'\r' { continue; }
        if byte == b'\n' { column_px = 0; row += 1; continue; }
        if row >= 19 { break; }
        if column_px >= width - 150 { column_px = 0; row += 1; }
        if (32..=126).contains(&byte) {
            if let Ok(value) = core::str::from_utf8(&[byte]) {
                draw_text(info, x + 76 + column_px, y + 124 + row * 16,
                          value, 1, [36, 52, 87]);
            }
            column_px += char_advance(byte, 1);
        }
    }
    if cursor == text.len() && row < 19 {
        fill_rect(info, x + 76 + column_px, y + 124 + row * 16,
                  2, 12, [55, 120, 221]);
    }
    fill_rect(info, x + 16, y + height - 60, width - 32, 28, [245, 248, 255]);
    draw_text(info, x + 32, y + height - 54,
              if saved { "Saved DOCUMENT.TXT" }
              else { "Ctrl+N New   Ctrl+O Open   Ctrl+S Save   Arrows edit" },
              1, [65, 87, 125]);
}

fn scan_to_ascii(scan: u8, shift: bool) -> Option<u8> {
    let letter = match scan {
        0x02 => b'1', 0x03 => b'2', 0x04 => b'3', 0x05 => b'4',
        0x06 => b'5', 0x07 => b'6', 0x08 => b'7', 0x09 => b'8',
        0x0a => b'9', 0x0b => b'0', 0x0c => b'-', 0x0d => b'=',
        0x10 => b'q', 0x11 => b'w', 0x12 => b'e', 0x13 => b'r',
        0x14 => b't', 0x15 => b'y', 0x16 => b'u', 0x17 => b'i',
        0x18 => b'o', 0x19 => b'p', 0x1e => b'a', 0x1f => b's',
        0x20 => b'd', 0x21 => b'f', 0x22 => b'g', 0x23 => b'h',
        0x24 => b'j', 0x25 => b'k', 0x26 => b'l', 0x2c => b'z',
        0x2d => b'x', 0x2e => b'c', 0x2f => b'v', 0x30 => b'b',
        0x31 => b'n', 0x32 => b'm', 0x33 => b',', 0x34 => b'.',
        0x35 => b'/', 0x39 => b' ',
        _ => return None,
    };
    Some(if shift && letter.is_ascii_lowercase() { letter.to_ascii_uppercase() }
         else { letter })
}

fn terminal_command(disk: Option<RootInfo>, line: &[u8], output: &mut [u8]) -> usize {
    if line == b"help" {
        return copy_output(output, b"help  dir  type NAME  ver  mem\nEsc returns to Files");
    }
    if line == b"ver" {
        return copy_output(output, b"AOS - x86-64 UEFI kernel preview");
    }
    if line == b"mem" {
        return copy_output(output, b"Own page tables and PIT timer are active.");
    }
    if line == b"dir" {
        let Some(root) = disk else { return copy_output(output, b"No disk"); };
        let mut length = 0usize;
        for index in 0..root.entries {
            let Some(file) = file_at(root, index) else { break };
            for &byte in &file.name[..8] {
                if byte != b' ' && length < output.len() { output[length] = byte; length += 1; }
            }
            if file.name[8] != b' ' && length < output.len() {
                output[length] = b'.'; length += 1;
                for &byte in &file.name[8..11] {
                    if byte != b' ' && length < output.len() { output[length] = byte; length += 1; }
                }
            }
            if length < output.len() { output[length] = b'\n'; length += 1; }
        }
        return length;
    }
    if line.starts_with(b"type ") {
        let Some(root) = disk else { return copy_output(output, b"No disk"); };
        let mut name = [b' '; 11];
        let mut part = 0usize;
        let mut index = 0usize;
        for &byte in &line[5..] {
            if byte == b'.' { part = 1; index = 0; continue; }
            let max = if part == 0 { 8 } else { 3 };
            if index >= max { return copy_output(output, b"Bad file name"); }
            name[part * 8 + index] = byte.to_ascii_uppercase();
            index += 1;
        }
        if let Some(file) = find_file(root, &name) {
            return read_file(root, file, output);
        }
        return copy_output(output, b"File not found");
    }
    copy_output(output, b"Unknown command. Type help.")
}

fn copy_output(output: &mut [u8], source: &[u8]) -> usize {
    let length = output.len().min(source.len());
    output[..length].copy_from_slice(&source[..length]);
    length
}

fn store_serial_init() {
    const PORT: u16 = 0x2f8;
    outb(PORT + 1, 0);
    outb(PORT + 3, 0x80);
    outb(PORT, 1);
    outb(PORT + 1, 0);
    outb(PORT + 3, 3);
    outb(PORT + 2, 0xc7);
    outb(PORT + 4, 0x0b);
}

fn store_byte() -> Option<u8> {
    const PORT: u16 = 0x2f8;
    let start = TICKS.load(Ordering::Relaxed);
    while TICKS.load(Ordering::Relaxed).wrapping_sub(start) < 200 {
        if inb(PORT + 5) & 1 != 0 { return Some(inb(PORT)); }
    }
    None
}

fn store_fetch(request: &[u8], output: &mut [u8]) -> Option<usize> {
    const PORT: u16 = 0x2f8;
    while inb(PORT + 5) & 1 != 0 { let _ = inb(PORT); }
    for &byte in request {
        let start = TICKS.load(Ordering::Relaxed);
        while inb(PORT + 5) & 0x20 == 0 {
            if TICKS.load(Ordering::Relaxed).wrapping_sub(start) > 200 { return None; }
        }
        outb(PORT, byte);
    }
    if store_byte()? != b'K' { return None; }
    let low = store_byte()?;
    let high = store_byte()?;
    let length = u16::from_le_bytes([low, high]) as usize;
    if length > output.len() { return None; }
    let mut sum = 0u16;
    for slot in &mut output[..length] {
        *slot = store_byte()?;
        sum = sum.wrapping_add(*slot as u16);
    }
    let expected = u16::from_le_bytes([store_byte()?, store_byte()?]);
    (sum == expected).then_some(length)
}

fn render_store(info: &BootInfo, catalog: &[u8; 42 * 16], count: usize,
                selected: usize, status: u8) {
    let (x, y, width, _) = window_rect(info);
    fill_rect(info, x + 16, y + 64, width - 32, 316, [245, 248, 255]);
    draw_text(info, x + 32, y + 88, "AOS STORE", 2, [28, 52, 105]);
    let hint = match status {
        0 => "Press Enter to load the catalogue",
        1 => "Up/Down select; Enter installs native AIS64",
        2 => "Installed. Open Programs to run it.",
        _ => "Store unavailable. Press Enter to retry.",
    };
    draw_text(info, x + 32, y + 126, hint, 1, [49, 68, 105]);
    for index in 0..count.min(8) {
        let record = &catalog[index * 42..][..42];
        if index == selected {
            fill_rect(info, x + 24, y + 160 + index * 28, width - 48, 26,
                      [215, 232, 253]);
        }
        let name_end = record[..9].iter().position(|&byte| byte == 0).unwrap_or(8);
        if let Ok(name) = core::str::from_utf8(&record[..name_end]) {
            draw_text(info, x + 32, y + 166 + index * 28, name, 2, [37, 56, 98]);
        }
        let description = &record[11..42];
        let end = description.iter().position(|&byte| byte == 0).unwrap_or(31);
        if let Ok(label) = core::str::from_utf8(&description[..end]) {
            draw_text(info, x + 248, y + 170 + index * 28,
                      label, 1, [70, 91, 126]);
        }
    }
}

fn render_terminal(info: &BootInfo, output: &[u8], input: &[u8]) {
    let (x, y, width, height) = window_rect(info);
    fill_rect(info, x + 16, y + 64, width - 32, height - 120, [22, 36, 70]);
    let mut column_px = 0usize;
    let mut row = 0usize;
    for &byte in output {
        if byte == b'\r' { continue; }
        if byte == b'\n' { column_px = 0; row += 1; continue; }
        if row >= 19 { break; }
        if column_px >= width - 80 { column_px = 0; row += 1; }
        if (32..=126).contains(&byte) {
            let letter = [byte];
            if let Ok(value) = core::str::from_utf8(&letter) {
                draw_text(info, x + 32 + column_px, y + 80 + row * 16,
                          value, 1, [234, 244, 255]);
            }
            column_px += char_advance(byte, 1);
        }
    }
    draw_text(info, x + 32, y + height - 92, "aos>", 1, [137, 204, 255]);
    let mut input_x = x + 88;
    for &byte in input {
        let letter = [byte];
        if let Ok(value) = core::str::from_utf8(&letter) {
            draw_text(info, input_x, y + height - 92,
                      value, 1, [234, 244, 255]);
        }
        input_x += char_advance(byte, 1);
    }
}

fn render_taskbar(info: &BootInfo, selected: usize) {
    let y = info.height - TASKBAR_BUFFER_HEIGHT;
    TASKBAR_ORIGIN_X.store(0, Ordering::Relaxed);
    TASKBAR_ORIGIN_Y.store(y, Ordering::Relaxed);
    SHELL_BUFFER_STRIDE.store(TASKBAR_BUFFER_WIDTH, Ordering::Relaxed);
    SHELL_BUFFER_HEIGHT.store(TASKBAR_BUFFER_HEIGHT, Ordering::Relaxed);
    TASKBAR_DRAW.store(true, Ordering::Relaxed);
    paint_wallpaper_region(info, 0, y, info.width, TASKBAR_BUFFER_HEIGHT);
    rounded_rect(info, 0, y, info.width, TASKBAR_BUFFER_HEIGHT, 0, [247, 250, 255], 220);
    fill_rect(info, 0, y, info.width, 1, [246, 249, 255]);
    if START_OPEN.load(Ordering::Relaxed) {
        rounded_rect(info, 3, y + 4, 50, 48, 12, [255, 255, 255], 205);
    }
    draw_text(info, 7, y + 12, "AOS", 2, [219, 54, 76]);
    rounded_rect(info, 62, y + 9, TASKBAR_APPS_X - 72, 38, 19, [255, 255, 255], 190);
    circle(info, 76, y + 25, 7, [87, 105, 130], 255);
    circle(info, 76, y + 25, 5, [247, 250, 255], 255);
    for step in 0..6 { fill_rect(info, 81 + step, y + 30 + step, 2, 2, [87, 105, 130]); }
    draw_text(info, 98, y + 18, "Find apps", 1, [73, 91, 119]);
    TASKBAR_FOCUS.store((selected * TASKBAR_TILE) as u64, Ordering::Relaxed);
    for i in 0..8 {
        let left = TASKBAR_APPS_X + i * TASKBAR_TILE;
        if i == selected {
            rounded_rect(info, left + 2, y + 3, TASKBAR_TILE - 4, 50, 14, [255, 255, 255], 210);
        }
        rounded_rect(info, left + 6, y + 5, 44, 44, 12, APP_COLOURS[i], 255);
        rounded_rect(info, left + 10, y + 8, 36, 10, 5, [255, 255, 255], 36);
        draw_app_symbol(info, i, left + 4, y + 3, APP_COLOURS[i]);
        if i == SHELL_ACTIVE.load(Ordering::Relaxed) && WINDOW_VISIBLE.load(Ordering::Relaxed) {
            fill_rect(info, left + 16, info.height - 3, 24, 3, [100, 113, 200]);
        }
    }
    draw_text(info, info.width - 157, y + 8, "AOS", 1, [46, 64, 100]);
    draw_text(info, info.width - 157, y + 29, "Desktop", 1, [89, 111, 152]);
    let (hour, minute, day, month, year) = read_clock();
    let clock = [b'0' + hour / 10, b'0' + hour % 10, b':',
                 b'0' + minute / 10, b'0' + minute % 10];
    let date = [b'0' + day / 10, b'0' + day % 10, b'/',
                b'0' + month / 10, b'0' + month % 10, b'/',
                b'2', b'0', b'0' + year / 10, b'0' + year % 10];
    if let Ok(clock) = core::str::from_utf8(&clock) {
        draw_text(info, info.width - 65, y + 8, clock, 1, [46, 64, 100]);
    }
    if let Ok(date) = core::str::from_utf8(&date) {
        draw_text(info, info.width - 91, y + 29, date, 1, [89, 111, 152]);
    }
    fill_rect(info, info.width - 5, y + 8, 1, 40, [185, 199, 225]);
    TASKBAR_DRAW.store(false, Ordering::Relaxed);
    for row in 0..TASKBAR_BUFFER_HEIGHT {
        let width = TASKBAR_BUFFER_WIDTH.min(info.width);
        let src = row * TASKBAR_BUFFER_WIDTH * 4;
        let dest = ((y + row) * info.stride) * 4;
        if dest + width * 4 > info.framebuffer_size { break; }
        unsafe {
            copy_nonoverlapping(((&raw const TASKBAR_BUFFER) as *const u8).add(src),
                                info.framebuffer.add(dest), width * 4);
        }
    }
}

const START_APPS: [usize; 7] = [0, 1, 7, 3, 5, 4, 2];

fn start_matches(app: usize, query: &[u8]) -> bool {
    query.is_empty() || LABELS[app].as_bytes().windows(query.len()).any(|part|
        part.iter().zip(query).all(|(a, b)| a.to_ascii_lowercase() == b.to_ascii_lowercase()))
}

fn start_app_at(query: &[u8], index: usize) -> Option<usize> {
    START_APPS.into_iter().filter(|&app| start_matches(app, query)).nth(index)
}

fn start_app_count(query: &[u8]) -> usize {
    START_APPS.into_iter().filter(|&app| start_matches(app, query)).count()
}

fn start_menu_rect(info: &BootInfo) -> (usize, usize, usize, usize) {
    let height = 560.min(info.height - TASKBAR_BUFFER_HEIGHT - 16);
    (0, info.height - TASKBAR_BUFFER_HEIGHT - height, 540.min(info.width), height)
}

fn render_start_menu(info: &BootInfo, query: &[u8], selection: usize) {
    let (x, y, width, height) = start_menu_rect(info);
    let origin_y = y.saturating_sub(8);
    let region_width = (width + 16).min(info.width);
    let region_height = (height + y - origin_y).min(info.height - origin_y);
    let capture = !START_BACKING_VALID.swap(true, Ordering::Relaxed);
    for row in 0..region_height {
        let offset = row * START_BACKING_SIDE * 4;
        let display = ((origin_y + row) * info.stride) * 4;
        unsafe {
            let backing = ((&raw mut START_BACKING) as *mut u8).add(offset);
            if capture { copy_nonoverlapping(info.framebuffer.add(display), backing, region_width * 4); }
            copy_nonoverlapping(backing, ((&raw mut TASKBAR_BUFFER) as *mut u8).add(offset),
                                region_width * 4);
        }
    }
    TASKBAR_ORIGIN_X.store(x, Ordering::Relaxed);
    TASKBAR_ORIGIN_Y.store(origin_y, Ordering::Relaxed);
    SHELL_BUFFER_STRIDE.store(START_BACKING_SIDE, Ordering::Relaxed);
    SHELL_BUFFER_HEIGHT.store(START_BACKING_SIDE, Ordering::Relaxed);
    TASKBAR_DRAW.store(true, Ordering::Relaxed);
    rounded_rect(info, x + 4, y.saturating_sub(8), width + 12, height + 8, 26,
                 [23, 35, 84], 32);
    rounded_rect(info, x, y, width, height, 22, [239, 245, 255], 255);
    draw_text(info, x + 22, y + 16, "AOS", 2, [245, 69, 83]);
    draw_text(info, x + 88, y + 16, "Start", 2, [46, 64, 100]);
    rounded_rect(info, x + 20, y + 62, width - 40, 36, 18, [255, 255, 255], 255);
    if query.is_empty() {
        draw_text(info, x + 32, y + 70, "Search applications", 1, [79, 98, 126]);
    } else if let Ok(text) = core::str::from_utf8(query) {
        draw_text(info, x + 32, y + 70, text, 1, [29, 48, 78]);
    }
    let count = start_app_count(query);
    for row in 0..count {
        let app = start_app_at(query, row).unwrap();
        let top = y + 112 + row * 48;
        if row == selection { rounded_rect(info, x + 12, top, 294, 48, 14, [216, 229, 250], 255); }
        rounded_rect(info, x + 22, top + 2, 44, 44, 12, APP_COLOURS[app], 255);
        draw_app_symbol(info, app, x + 20, top, APP_COLOURS[app]);
        draw_text(info, x + 82, top + 9, LABELS[app], 2, [33, 52, 88]);
    }
    if count == 0 { draw_text(info, x + 26, y + 126, "No apps found", 1, [89, 111, 152]); }
    draw_text(info, x + 330, y + 118, "Pinned", 1, [89, 111, 152]);
    for (index, app) in [7usize, 1, 0, 2].into_iter().enumerate() {
        let left = x + 330 + index % 2 * 96;
        let top = y + 150 + index / 2 * 106;
        rounded_rect(info, left, top, 88, 96, 16, [223, 233, 251], 255);
        rounded_rect(info, left + 20, top + 8, 48, 48, 13, APP_COLOURS[app], 255);
        draw_app_symbol(info, app, left + 20, top + 8, APP_COLOURS[app]);
        let text_x = left + (88 - text_width(LABELS[app], 1)) / 2;
        draw_text(info, text_x, top + 66, LABELS[app], 1, [46, 64, 100]);
    }
    fill_rect(info, x + 20, y + height - 58, width - 40, 1, [211, 223, 245]);
    draw_text(info, x + 24, y + height - 37, "Local session", 1, [89, 111, 152]);
    rounded_rect(info, x + width - 148, y + height - 51, 44, 44, 12, APP_COLOURS[6], 255);
    draw_app_symbol(info, 6, x + width - 150, y + height - 53, APP_COLOURS[6]);
    draw_text(info, x + width - 96, y + height - 37, "Power", 1, [46, 64, 100]);
    TASKBAR_DRAW.store(false, Ordering::Relaxed);
    for row in 0..region_height {
        let source = row * START_BACKING_SIDE * 4;
        let display = ((origin_y + row) * info.stride) * 4;
        unsafe {
            copy_nonoverlapping(((&raw const TASKBAR_BUFFER) as *const u8).add(source),
                                info.framebuffer.add(display), region_width * 4);
        }
    }
}

fn read_clock() -> (u8, u8, u8, u8, u8) {
    let read = |register: u8| { outb(0x70, register); inb(0x71) };
    // Take a stable RTC sample outside its update window.
    for _ in 0..1000 { if read(0x0a) & 0x80 == 0 { break; } }
    let mode = read(0x0b);
    let raw_hour = read(4);
    let convert = |value: u8| if mode & 4 != 0 { value }
                              else { (value >> 4) * 10 + (value & 15) };
    let mut hour = convert(raw_hour & 0x7f);
    if mode & 2 == 0 { hour = hour % 12 + if raw_hour & 0x80 != 0 { 12 } else { 0 }; }
    (hour, convert(read(2)), convert(read(7)), convert(read(8)), convert(read(9)))
}

fn draw_app_symbol(info: &BootInfo, app: usize, x: usize, y: usize,
                   background: [u8; 3]) {
    const WHITE: [u8; 3] = [255, 255, 255];
    match app {
        0 => { // Files: folder
            rounded_rect(info, x + 10, y + 12, 17, 10, 3, WHITE, 235);
            rounded_rect(info, x + 8, y + 18, 32, 22, 5, WHITE, 255);
            fill_rect(info, x + 11, y + 22, 26, 2, [195, 222, 255]);
        }
        1 => { // Notes: ruled pad
            rounded_rect(info, x + 11, y + 8, 26, 32, 4, WHITE, 255);
            for line in 0..3 {
                rounded_rect(info, x + 16, y + 20 + line * 6, 17, 2, 1,
                             [190, 142, 65], 220);
            }
            for dot in 0..3 {
                circle(info, x + 17 + dot * 7, y + 13, 1, [200, 147, 59], 255);
            }
        }
        2 => { // System: gear
            fill_rect(info, x + 22, y + 7, 4, 34, WHITE);
            fill_rect(info, x + 7, y + 22, 34, 4, WHITE);
            circle(info, x + 24, y + 24, 12, WHITE, 255);
            circle(info, x + 24, y + 24, 6, background, 255);
        }
        3 => { // Terminal: prompt in a screen
            rounded_rect(info, x + 7, y + 10, 34, 28, 5, [14, 26, 50], 255);
            draw_text(info, x + 12, y + 10, ">", 2, WHITE);
            rounded_rect(info, x + 26, y + 30, 10, 2, 1, WHITE, 255);
        }
        4 => { // Store: handled bag
            circle(info, x + 24, y + 17, 9, WHITE, 255);
            circle(info, x + 24, y + 17, 6, background, 255);
            rounded_rect(info, x + 11, y + 17, 26, 23, 4, WHITE, 255);
            fill_rect(info, x + 14, y + 22, 20, 2, [197, 235, 239]);
        }
        5 => { // Programs: app grid
            for row in 0..2 {
                for col in 0..2 {
                    rounded_rect(info, x + 10 + col * 17, y + 10 + row * 17,
                                 12, 12, 3, WHITE, 255);
                }
            }
        }
        6 => { // Power: broken ring and switch
            circle(info, x + 24, y + 25, 13, WHITE, 255);
            circle(info, x + 24, y + 25, 9, background, 255);
            fill_rect(info, x + 19, y + 9, 10, 13, background);
            rounded_rect(info, x + 22, y + 8, 4, 18, 2, WHITE, 255);
        }
        7 => { // Write: page and ink lines
            rounded_rect(info, x + 11, y + 7, 27, 35, 4, WHITE, 255);
            rounded_rect(info, x + 15, y + 17, 17, 2, 1, [154, 181, 235], 255);
            rounded_rect(info, x + 15, y + 23, 17, 2, 1, [154, 181, 235], 255);
            rounded_rect(info, x + 15, y + 29, 12, 2, 1, [154, 181, 235], 255);
            rounded_rect(info, x + 30, y + 28, 4, 12, 2, background, 255);
        }
        _ => {}
    }
}

fn stroke_rect(info: &BootInfo, x: usize, y: usize, width: usize,
                height: usize, colour: [u8; 3]) {
    fill_rect(info, x, y, width, 1, colour);
    fill_rect(info, x, y + height - 1, width, 1, colour);
    fill_rect(info, x, y, 1, height, colour);
    fill_rect(info, x + width - 1, y, 1, height, colour);
}

fn render_window_controls(info: &BootInfo, x: usize, y: usize) {
    let colour = [49, 64, 91];
    let centre = x + WINDOW_BUTTON_WIDTH / 2;
    fill_rect(info, centre - 6, y + 24, 12, 2, colour);
    let centre = centre + WINDOW_BUTTON_WIDTH;
    if WINDOW_MAXIMIZED.load(Ordering::Relaxed) {
        stroke_rect(info, centre - 3, y + 17, 11, 11, colour);
        fill_rect(info, centre - 7, y + 21, 11, 11, [235, 242, 252]);
        stroke_rect(info, centre - 7, y + 21, 11, 11, colour);
    } else {
        stroke_rect(info, centre - 6, y + 18, 12, 12, colour);
    }
    let centre = centre + WINDOW_BUTTON_WIDTH;
    for step in 0..12 {
        fill_rect(info, centre - 6 + step, y + 18 + step, 1, 1, colour);
        fill_rect(info, centre + 5 - step, y + 18 + step, 1, 1, colour);
    }
}

const FONT_SMALL: &[u8] = include_bytes!("../assets/inter-small.bin");
const FONT_LARGE: &[u8] = include_bytes!("../assets/inter-large.bin");
const ADVANCE_SMALL: &[u8] = include_bytes!("../assets/inter-small-advance.bin");
const ADVANCE_LARGE: &[u8] = include_bytes!("../assets/inter-large-advance.bin");

fn char_advance(byte: u8, scale: usize) -> usize {
    let index = if (32..=126).contains(&byte) { (byte - 32) as usize }
                else { (b'?' - 32) as usize };
    if scale >= 2 { ADVANCE_LARGE[index] as usize }
    else { ADVANCE_SMALL[index] as usize }
}

fn text_width(message: &str, scale: usize) -> usize {
    message.bytes().map(|byte| char_advance(byte, scale)).sum()
}

fn draw_text(info: &BootInfo, x: usize, y: usize, message: &str,
             scale: usize, colour: [u8; 3]) {
    let (atlas, cell_width, cell_height, advances) = if scale >= 2 {
        (FONT_LARGE, 24usize, 28usize, ADVANCE_LARGE)
    } else {
        (FONT_SMALL, 16usize, 18usize, ADVANCE_SMALL)
    };
    let mut pen = x;
    for byte in message.bytes() {
        let index = if (32..=126).contains(&byte) { (byte - 32) as usize }
                    else { (b'?' - 32) as usize };
        let start = index * cell_width * cell_height;
        for row in 0..cell_height {
            for col in 0..cell_width {
                let alpha = atlas[start + row * cell_width + col];
                if alpha != 0 {
                    blend_pixel(info, pen + col, y + row, colour, alpha);
                }
            }
        }
        pen += advances[index] as usize;
    }
}

fn blend_pixel(info: &BootInfo, x: usize, y: usize, rgb: [u8; 3], alpha: u8) {
    if x >= info.width || y >= info.height || info.pixel_format > 1 { return; }
    let Some(pixel) = draw_pixel_ptr(info, x, y) else { return; };
    let colour = if info.pixel_format == 1 { [rgb[2], rgb[1], rgb[0]] } else { rgb };
    for (channel, value) in colour.into_iter().enumerate() {
        unsafe {
            let component = pixel.add(channel);
            let old = core::ptr::read_volatile(component) as u16;
            let mixed = (old * (255 - alpha as u16) + value as u16 * alpha as u16) / 255;
            write_volatile(component, mixed as u8);
        }
    }
}

fn draw_pixel_ptr(info: &BootInfo, x: usize, y: usize) -> Option<*mut u8> {
    if TASKBAR_DRAW.load(Ordering::Relaxed) {
        let origin_x = TASKBAR_ORIGIN_X.load(Ordering::Relaxed);
        let origin_y = TASKBAR_ORIGIN_Y.load(Ordering::Relaxed);
        let local_x = x.checked_sub(origin_x)?;
        let local_y = y.checked_sub(origin_y)?;
        let stride = SHELL_BUFFER_STRIDE.load(Ordering::Relaxed);
        if local_x >= stride || local_y >= SHELL_BUFFER_HEIGHT.load(Ordering::Relaxed) {
            return None;
        }
        let offset = (local_y * stride + local_x) * 4;
        return Some(unsafe { ((&raw mut TASKBAR_BUFFER) as *mut u8).add(offset) });
    }
    let offset = (y * info.stride + x) * 4;
    if offset + 4 > info.framebuffer_size { return None; }
    Some(unsafe { info.framebuffer.add(offset) })
}

fn put_pixel(info: &BootInfo, x: usize, y: usize, rgb: [u8; 3]) {
    if x >= info.width || y >= info.height || info.pixel_format > 1 { return; }
    let Some(pixel) = draw_pixel_ptr(info, x, y) else { return; };
    let colour = if info.pixel_format == 1 { [rgb[2], rgb[1], rgb[0]] } else { rgb };
    for (channel, value) in colour.into_iter().enumerate() {
        unsafe { write_volatile(pixel.add(channel), value); }
    }
}

fn rounded_rect(info: &BootInfo, x: usize, y: usize, width: usize,
                height: usize, radius: usize, rgb: [u8; 3], alpha: u8) {
    if width == 0 || height == 0 { return; }
    let r = radius.min((width - 1) / 2).min((height - 1) / 2) as isize;
    let left = x as isize + r;
    let right = (x + width - 1) as isize - r;
    let top = y as isize + r;
    let bottom = (y + height - 1) as isize - r;
    for py in y..y.saturating_add(height).min(info.height) {
        for px in x..x.saturating_add(width).min(info.width) {
            let nearest_x = (px as isize).clamp(left, right);
            let nearest_y = (py as isize).clamp(top, bottom);
            let dx = px as isize - nearest_x;
            let dy = py as isize - nearest_y;
            if dx * dx + dy * dy <= r * r {
                if alpha == 255 { put_pixel(info, px, py, rgb); }
                else { blend_pixel(info, px, py, rgb, alpha); }
            }
        }
    }
}

fn circle(info: &BootInfo, cx: usize, cy: usize, radius: usize,
          rgb: [u8; 3], alpha: u8) {
    let x = cx.saturating_sub(radius);
    let y = cy.saturating_sub(radius);
    rounded_rect(info, x, y, radius * 2 + 1, radius * 2 + 1, radius, rgb, alpha);
}


fn fill_rect(info: &BootInfo, x: usize, y: usize, width: usize, height: usize,
             rgb: [u8; 3]) {
    if info.pixel_format > 1 {
        return;
    }
    for py in y..y.saturating_add(height).min(info.height) {
        for px in x..x.saturating_add(width).min(info.width) {
            let Some(pixel) = draw_pixel_ptr(info, px, py) else { continue; };
            let bytes = if info.pixel_format == 1 {
                [rgb[2], rgb[1], rgb[0]]
            } else {
                rgb
            };
            unsafe {
                write_volatile(pixel, bytes[0]);
                write_volatile(pixel.add(1), bytes[1]);
                write_volatile(pixel.add(2), bytes[2]);
            }
        }
    }
}

fn outb(port: u16, value: u8) {
    unsafe { asm!("out dx, al", in("dx") port, in("al") value) }
}

fn ps2_write(port: u16, value: u8) -> bool {
    for _ in 0..100_000 {
        if inb(0x64) & 2 == 0 {
            outb(port, value);
            return true;
        }
    }
    false
}

fn ps2_enable_mouse() -> bool {
    for _ in 0..32 {
        if inb(0x64) & 1 == 0 { break; }
        let _ = inb(0x60);
    }
    if !ps2_write(0x64, 0xa8) { return false; }
    if !ps2_write(0x64, 0xd4) || !ps2_write(0x60, 0xf4) { return false; }
    for _ in 0..100_000 {
        let status = inb(0x64);
        if status & 1 == 0 { continue; }
        let byte = inb(0x60);
        if status & 0x20 != 0 { return byte == 0xfa; }
    }
    false
}

fn inb(port: u16) -> u8 {
    let value: u8;
    unsafe { asm!("in al, dx", in("dx") port, out("al") value) }
    value
}

fn serial_init() {
    outb(0x3f9, 0x00);
    outb(0x3fb, 0x80);
    outb(0x3f8, 0x03);
    outb(0x3f9, 0x00);
    outb(0x3fb, 0x03);
    outb(0x3fa, 0xc7);
    outb(0x3fc, 0x0b);
}

fn serial_write(bytes: &[u8]) {
    for &byte in bytes {
        while inb(0x3fd) & 0x20 == 0 {}
        outb(0x3f8, byte);
    }
}

fn serial_hex(mut value: u64) {
    let mut text = [b'0'; 16];
    for digit in text.iter_mut().rev() {
        let nibble = (value & 0xf) as u8;
        *digit = if nibble < 10 { b'0' + nibble } else { b'A' + nibble - 10 };
        value >>= 4;
    }
    serial_write(&text);
}
