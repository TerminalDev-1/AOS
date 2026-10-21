//! Minimal ring-3 AIS64 execution boundary. The desktop still runs in ring 0.

use super::{FrameAllocator, IdtEntry, IdtPointer, TICKS};
use core::arch::{asm, global_asm};
use core::ptr::{copy_nonoverlapping, write_bytes};
use core::sync::atomic::Ordering;

const CODE_VA: u64 = 0x2000_0000;
const STACK_VA: u64 = CODE_VA + 0x1000;

#[unsafe(no_mangle)]
static mut USER_RETURN_RSP: u64 = 0;
#[unsafe(no_mangle)]
static mut USER_KERNEL_CR3: u64 = 0;
#[unsafe(no_mangle)]
static mut USER_FAULT: u64 = 0;
static mut USER_ACTIVE: bool = false;
static mut USER_DEADLINE: u64 = 0;
static mut USER_OUTPUT: *mut u8 = core::ptr::null_mut();
static mut USER_OUTPUT_LEN: usize = 0;

global_asm!(r#"
.global aos_enter_user
aos_enter_user:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    mov qword ptr [rip + USER_RETURN_RSP], rsp
    mov cr3, rdx
    push 0x1b
    push rsi
    pushfq
    pop rax
    or rax, 0x200
    push rax
    push 0x23
    push rdi
    iretq
.global aos_user_syscall
aos_user_syscall:
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
    mov rcx, [rsp + 128]
    and rcx, 3
    cmp rcx, 3
    jne 1f
    mov rax, [rsp + 112]
    test rax, rax
    jz 2f
    cmp rax, 1
    jne 1f
    mov rdi, [rsp + 72]
    mov rsi, [rsp + 80]
    mov rbx, rsp
    and rsp, -16
    call aos_user_write
    mov rsp, rbx
1:
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
2:
.global aos_restore_kernel
aos_restore_kernel:
    mov rax, qword ptr [rip + USER_KERNEL_CR3]
    mov cr3, rax
    mov rsp, qword ptr [rip + USER_RETURN_RSP]
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    sti
    ret
.global aos_user_abort
aos_user_abort:
    mov qword ptr [rip + USER_FAULT], 2
    jmp aos_restore_kernel
.global aos_user_fault_no_error
aos_user_fault_no_error:
    push 0
    jmp aos_user_fault_common
.global aos_user_fault_error
aos_user_fault_error:
    jmp aos_user_fault_common
aos_user_fault_common:
    mov rax, [rsp + 16]
    and rax, 3
    cmp rax, 3
    jne 3f
    mov qword ptr [rip + USER_FAULT], 1
    jmp aos_restore_kernel
3:
    cli
4:
    hlt
    jmp 4b
"#);

unsafe extern "sysv64" {
    fn aos_enter_user(entry: u64, stack: u64, page_tables: u64);
    fn aos_user_syscall();
    fn aos_user_fault_no_error();
    fn aos_user_fault_error();
}

#[unsafe(no_mangle)]
extern "sysv64" fn aos_user_write(pointer: u64, length: usize) {
    let end = match pointer.checked_add(length as u64) { Some(v) => v, None => return };
    let code_ok = pointer >= CODE_VA && end <= CODE_VA + 4096;
    let stack_ok = pointer >= STACK_VA && end <= STACK_VA + 4096;
    if !code_ok && !stack_ok { return; }
    unsafe {
        if USER_OUTPUT.is_null() { return; }
        let remaining = 256usize.saturating_sub(USER_OUTPUT_LEN);
        let count = length.min(remaining);
        copy_nonoverlapping(pointer as *const u8, USER_OUTPUT.add(USER_OUTPUT_LEN), count);
        USER_OUTPUT_LEN += count;
    }
}

pub fn timer_expired(ticks: u64, interrupted_cs: u64) -> bool {
    unsafe { USER_ACTIVE && interrupted_cs & 3 == 3 && ticks >= USER_DEADLINE }
}

pub struct Runtime {
    code_phys: u64,
    stack_phys: u64,
    user_cr3: u64,
}

impl Runtime {
    pub fn page_tables(&self) -> u64 { self.user_cr3 }

    pub fn install(frames: &mut FrameAllocator<'_>, pml4: u64, idt: u64) -> Option<Self> {
        let code_phys = frames.allocate()?;
        let stack_phys = frames.allocate()?;
        let user_root = frames.allocate()?;
        let user_pdpt = frames.allocate()?;
        let user_pd = frames.allocate()?;
        let table = frames.allocate()?;
        let gdt = frames.allocate()?;
        let tss = frames.allocate()?;
        let kernel_stack = frames.allocate()?;

        unsafe {
            let root = pml4 as *mut u64;
            let kernel_pdpt = (root.read() & !0xfff) as *mut u64;
            let kernel_pd = (kernel_pdpt.read() & !0xfff) as *mut u64;
            copy_nonoverlapping(root as *const u8, user_root as *mut u8, 4096);
            copy_nonoverlapping(kernel_pdpt as *const u8, user_pdpt as *mut u8, 4096);
            copy_nonoverlapping(kernel_pd as *const u8, user_pd as *mut u8, 4096);
            (user_root as *mut u64).write(user_pdpt | 7);
            (user_pdpt as *mut u64).write(user_pd | 7);
            let slot = (user_pd as *mut u64).add((CODE_VA / 0x20_0000) as usize);
            slot.write(table | 7);
            (table as *mut u64).write(code_phys | 5);
            (table as *mut u64).add(1).write(stack_phys | 7);
            USER_KERNEL_CR3 = pml4;

            // Kernel code/data, user data/code, and a 64-bit TSS descriptor.
            let descriptors = gdt as *mut u64;
            descriptors.add(1).write(0x00af_9a00_0000_ffff);
            descriptors.add(2).write(0x00cf_9200_0000_ffff);
            descriptors.add(3).write(0x00cf_f200_0000_ffff);
            descriptors.add(4).write(0x00af_fa00_0000_ffff);
            let cs: u16;
            asm!("mov {0:x}, cs", out(reg) cs, options(nostack, nomem));
            let ss: u16;
            asm!("mov {0:x}, ss", out(reg) ss, options(nostack, nomem));
            if (cs as usize / 8) < 16 {
                descriptors.add(cs as usize / 8).write(0x00af_9a00_0000_ffff);
            }
            if (ss as usize / 8) < 16 {
                descriptors.add(ss as usize / 8).write(0x00cf_9200_0000_ffff);
            }
            let tss_ptr = tss as *mut u8;
            (tss_ptr.add(4) as *mut u64).write_unaligned(kernel_stack + 4096);
            (tss_ptr.add(102) as *mut u16).write_unaligned(104);
            let base = tss;
            let limit = 103u64;
            let low = limit | ((base & 0xff_ffff) << 16) | (0x89 << 40)
                | (((base >> 24) & 0xff) << 56);
            descriptors.add(10).write(low);
            descriptors.add(11).write(base >> 32);
            let pointer = IdtPointer { limit: 16 * 8 - 1, base: gdt };
            asm!("lgdt [{}]", in(reg) &pointer, options(readonly, nostack));
            asm!("ltr {0:x}", in(reg) 0x50u16, options(nostack));

            let handler = aos_user_syscall as *const () as u64;
            ((idt as *mut IdtEntry).add(0x80)).write(IdtEntry {
                offset_low: handler as u16,
                selector: 0x08,
                ist: 0,
                flags: 0xee,
                offset_mid: (handler >> 16) as u16,
                offset_high: (handler >> 32) as u32,
                reserved: 0,
            });
            for (vector, handler) in [
                (0, aos_user_fault_no_error as *const () as u64),
                (6, aos_user_fault_no_error as *const () as u64),
                (13, aos_user_fault_error as *const () as u64),
                (14, aos_user_fault_error as *const () as u64),
            ] {
                ((idt as *mut IdtEntry).add(vector)).write(IdtEntry {
                    offset_low: handler as u16,
                    selector: 0x08,
                    ist: 0,
                    flags: 0x8e,
                    offset_mid: (handler >> 16) as u16,
                    offset_high: (handler >> 32) as u32,
                    reserved: 0,
                });
            }
        }
        Some(Self { code_phys, stack_phys, user_cr3: user_root })
    }

    pub fn run(&mut self, code: &[u8], entry: usize,
               output: &mut [u8; 256]) -> Option<usize> {
        if code.is_empty() || code.len() > 4096 || entry >= code.len() { return None; }
        unsafe {
            write_bytes(self.code_phys as *mut u8, 0, 4096);
            write_bytes(self.stack_phys as *mut u8, 0, 4096);
            copy_nonoverlapping(code.as_ptr(), self.code_phys as *mut u8, code.len());
            USER_OUTPUT = output.as_mut_ptr();
            USER_OUTPUT_LEN = 0;
            USER_FAULT = 0;
            USER_DEADLINE = TICKS.load(Ordering::Relaxed) + 300;
            USER_ACTIVE = true;
            aos_enter_user(CODE_VA + entry as u64, STACK_VA + 4096 - 8,
                             self.user_cr3);
            USER_ACTIVE = false;
            USER_OUTPUT = core::ptr::null_mut();
            if USER_FAULT != 0 { None } else { Some(USER_OUTPUT_LEN) }
        }
    }
}
