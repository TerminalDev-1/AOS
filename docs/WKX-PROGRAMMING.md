# Writing WinKit programs (`.WKX`)

This is the reference for anyone, human or AI agent, who wants to write a
program that runs inside the WinKit terminal. It is meant to be complete: you
should not need to read the kernel source to write a working program.

Contents: [Quick start](#quick-start) ·
[The execution environment](#the-execution-environment) ·
[System calls](#system-calls) · [The assembler](#the-assembler-wkasm) ·
[Building and running](#building-and-running) ·
[Testing without a screen](#testing-without-a-screen) ·
[Examples](#examples) · [Pitfalls](#pitfalls) ·
[Checklist](#checklist-for-an-agent)

## Quick start

A WKX program is a **flat 16-bit binary, assembled with `ORG 100h`**, with no
header. Create `src/progs/HI.ASM`:

```asm
CODE SEGMENT PARA PUBLIC 'CODE'
ASSUME CS:CODE, DS:CODE
ORG 100h

start:
    mov dx,OFFSET msg      ; DS:DX -> NUL-terminated string
    mov ah,2               ; AH=2: print string
    int 80h
    xor ah,ah              ; AH=0: exit
    int 80h

msg db 'Hi from WinKit!',13,10,0
CODE ENDS
END start
```

Then:

```
python scripts/mkimage.py          # assembles it and puts HI.WKX on the disk
python scripts/run.py --snapshot   # boot WinKit, press Enter, open WinKit (F6)
winkit> hi
Hi from WinKit!
```

Every `.ASM` in `src/progs/` becomes `NAME.WKX` on the disk automatically.

## The execution environment

WinKit is a real-mode 8086 system with no operating-system services other
than the ones listed here. Your program runs with full CPU and hardware
access, so be careful.

| Item | Value |
|---|---|
| CPU mode | 16-bit real mode; **8086 instructions only** (no `push imm`, no `shl ax,2`, no `pusha`, no 32-bit registers) |
| Load address | `3000:0100` |
| Segments on entry | `CS = DS = ES = SS = 3000h` (one 64 KB segment for code, data and stack) |
| Stack | `SS:SP = 3000:FFFC`, growing down; a return address of `0000` is on it |
| Registers on entry | `AX = BX = CX = DX = SI = DI = BP = 0` |
| Interrupts | enabled |
| Direction flag | clear |
| Size limit | about **52 KB** of file (53,248 bytes); anything larger is truncated |
| Offset `0000`–`00FF` | not part of your file; offset 0 holds a 4-byte exit stub |
| Arguments / environment | **none**; there is no command tail or environment block |

### Memory map

| Address | Contents | Yours? |
|---|---|---|
| `0000:0000` | interrupt vector table, BIOS data | no |
| `0000:7C00` | the boot sector (the kernel reads disk geometry from it) | no |
| `1000:0100` | the WinKit kernel | no |
| `2000:0000` | cached FAT, root directory and a file buffer | no |
| `3000:0000` | **your program**: stub at 0, code from `0100h`, stack at the top | yes |
| `A000:0000` | VGA video memory (the desktop is in mode 12h) | do not touch |

Memory above your image is not cleared; treat all of it as uninitialised.

### Exiting

Either of these returns to the terminal:

* `xor ah,ah` / `int 80h` (explicit exit), or
* `ret` from the entry level of your program (the stack holds `0000`, which
  is the exit stub). Your stack pointer must be exactly where it started.

If your program never exits it hangs the machine. There is no way to
interrupt a running program; reset the emulator.

## System calls

One software interrupt, `INT 80h`, with the function number in `AH`. **All
registers are preserved** (flags too), except where noted.

| AH | Name | Input | Output |
|---|---|---|---|
| `00h` | exit | | does not return |
| `01h` | print character | `AL` = character | |
| `02h` | print string | `DS:DX` -> NUL-terminated string | |
| `03h` | read key | | `AL` = ASCII, `AH` = BIOS scan code (clobbered) |

Any other `AH` is ignored and returns immediately.

### Output model

Output goes to the terminal screen, a **50 column x 16 row** text grid in the
WinKit window.

* `AH=1` / `AH=2` understand `13` (carriage return), `10` (line feed, also
  scrolls) and `8` (backspace). Use `13,10` for a new line.
* Other bytes below 32 are ignored (there is no tab). Bytes 32-255 are drawn
  with the BIOS 8x16 font.
* Text wraps at column 50 and the screen scrolls when it runs past row 16.
* The grid is redrawn after every call, but only while the WinKit window is
  the active, visible window.

### Input model

* `AH=3` **blocks** until a key is pressed, then returns the BIOS key code
  (`AL` = ASCII, `0` for extended keys such as the function keys;
  `AH` = scan code). There is no non-blocking read and no echo: print it yourself.
* Mouse and desktop are not delivered to programs.

### Things the system does not provide

No file I/O, no graphics, no timer, no memory allocator, no arguments, no DOS
calls (`INT 21h` does not exist; there is no DOS). A program that needs more
can use the BIOS directly, since it has the whole machine:

* `INT 16h` keyboard, `INT 1Ah` real-time clock and `INT 12h` memory size
  are safe to call from a program. `INT 13h` raw disk access works too, but
  the kernel caches the FAT, so writing sectors behind its back can corrupt
  the disk.
* Do **not** change the video mode (`INT 10h AH=0`) or write to `A000h`: the
  desktop owns the screen and will be corrupted.
* Keep `SS:SP` valid and at least 64 bytes of stack free below `SP` when
  you call `INT 80h`; the handler runs on your stack. (`AH=2` reads the
  string through whatever `DS` is at the call.)
* While a program runs, the desktop is frozen: the pointer does not move on
  screen and the clock does not update until the program exits.

## The assembler (`wkasm`)

Programs are built with WinKit's own assembler, `scripts/wkasm.py`, a
pure-Python tool with no dependencies. It accepts MASM-style syntax:

```
python scripts/wkasm.py src/progs/HI.ASM -o HI.WKX     # one file
python scripts/wkasm.py src/progs/HI.ASM -m            # also print the symbols
```

### Supported

| Area | What works |
|---|---|
| Instructions | `mov push pop xchg lea`, `add or adc sbb and sub xor cmp test`, `inc dec not neg mul imul div idiv`, shifts and rotates by `1` or `CL`, `jmp call ret retf iret`, all conditional jumps, `loop loope loopne jcxz`, `int in out`, string ops `lodsb/w stosb/w movsb/w cmpsb/w scasb/w` with `rep repe repne`, `cli sti cld std clc stc cmc hlt nop cbw cwd pushf popf lahf sahf xlat` |
| Operands | registers; immediates; `[bx]`, `[bx+si]`, `[bp+8]`, `[si+CONST]`; `name[bx]`; `seg:[...]`; `BYTE PTR` / `WORD PTR`; named variables (typed by their `DB`/`DW`) |
| Data | `DB`, `DW`, `DD`; strings `'text'`; `n DUP (value)`; `?` |
| Constants | `name EQU expr` |
| Expressions | `+ - * / MOD SHL SHR AND OR XOR NOT`, parentheses, `OFFSET`, `HIGH`, `LOW`, `$`, numbers (`10`, `0FFh`, `0x1F`), character constants (`'A'`) |
| Structure | `ORG`, `INCLUDE file`, `MACRO`/`ENDM`/`LOCAL`, `PROC`/`ENDP`, labels `name:`; `SEGMENT`, `ASSUME`, `END` are accepted and ignored |
| Misc | names are case-insensitive; `;` starts a comment; `jmp short` / `jmp near` |

### Rules to know

* **8086 only.** `push 5`, `shl ax,3`, `imul ax,bx,7`, `movzx`, `pusha` and
  any 386 instruction are errors.
* **One segment.** Code, data and stack share segment `3000h`. Put your data
  *after* the code that exits (or jump over it), because execution starts at
  the first byte.
* **Operand size must be known.** `mov [bx],5` is an error: write
  `mov byte ptr [bx],5` or `mov word ptr [bx],5`. A named variable carries its
  size, so `mov counter,5` is fine if `counter` is a `DB` or `DW`.
* **`OFFSET`** gives an address as a number: `mov dx,OFFSET msg`. A bare
  variable name is the *contents*: `mov al,flag`.
* **Conditional jumps reach +-127 bytes.** `je far_label` beyond that is an
  error ("jump out of range"); invert the test and use `jmp`:
  ```asm
      jne skip
      jmp far_label
  skip:
  ```
  or write a macro for it (see `src/MACROS.INC`).
* **Order of definition.** Constants (`EQU`) and `DUP` counts must be defined
  before they are used. Labels and variables may be used before they are
  defined.
* `ORG 100h` must be present in a `.WKX` and should be the first statement.

Error messages name the file and line: `HI.ASM(12): jump out of range by 9
byte(s)`.

## Building and running

| Goal | Command |
|---|---|
| Put every program in `src/progs/` on a fresh image | `python scripts/mkimage.py` |
| Boot the image | `python scripts/run.py` (add `--snapshot` to throw away disk writes) |
| Assemble one file | `python scripts/wkasm.py FILE.ASM -o NAME.WKX` |
| Run the system tests | `python scripts/test.py` |
| Run the assembler self-test | `python scripts/test_wkasm.py` |

Inside WinKit: press Enter on the welcome dialog, open the terminal with F6
or Start > WinKit, then type the program's name (the `.WKX` is optional).
Names are 8.3 and case-insensitive. `dir` lists what is on the disk.

## Testing without a screen

`scripts/test.py` drives WinKit in a headless QEMU and reads the terminal
back out of guest memory. Reuse that to test your program:

```python
import sys, time
sys.path.insert(0, "scripts")
from qemu import Machine            # scripts/qemu.py

with Machine("out/winkit.img") as m:
    while m.video_mode() != 0x12:   # desktop is up
        time.sleep(.2)
    time.sleep(1)
    m.key("ret")                    # leave the welcome dialog
    m.key("f6")                     # open WinKit
    m.type("hi"); m.key("ret"); time.sleep(.7)
    # the terminal text lives right after the signature "WKTERMBUF":
    # 16 rows of 52 bytes, each row NUL-terminated
    m.qmp("pmemsave", val=0, size=640*1024, filename="ram.bin")
    ram = open("ram.bin", "rb").read()
    base = ram.rfind(b"WKTERMBUF") + 9
    rows = [ram[base+i*52:base+(i+1)*52].split(b"\0")[0].decode("latin-1")
            for i in range(16)]
    assert "Hi from WinKit!" in "\n".join(rows)
```

Send `cls` before the command if you want only its output on screen.
`Machine` uses `-snapshot` by default, so tests never modify the image.

## Examples

All of these are in `src/progs/` and are built into the image.

| File | Shows |
|---|---|
| `HELLO.ASM` | the minimum program: print a string, exit |
| `COUNT.ASM` | a loop using `AH=1` to print characters one at a time |
| `KEYS.ASM` | `AH=3`: echo keys until Esc |
| `FIB.ASM` | a reusable `print_number`, subroutines with `call`/`ret`, `div` |
| `REVERSE.ASM` | reading a line with `AH=3`, indexed data, the full example below |
| `HI.ASM` | the quick-start program |

### A complete example with input

```asm
; REVERSE.ASM - read a line, print it backwards.
CODE SEGMENT PARA PUBLIC 'CODE'
ASSUME CS:CODE, DS:CODE
ORG 100h

start:
    mov dx,OFFSET ask
    mov ah,2
    int 80h
    xor si,si                  ; SI = number of characters read
read:
    mov ah,3                   ; wait for a key
    int 80h
    cmp al,13                  ; Enter ends the line
    je show
    cmp al,32
    jb read                    ; ignore control keys
    cmp si,40
    jae read                   ; line is full
    mov line[si],al            ; store it
    inc si
    mov ah,1                   ; echo it
    int 80h
    jmp read
show:
    mov dx,OFFSET crlf
    mov ah,2
    int 80h
back:
    or si,si
    jz done
    dec si
    mov al,line[si]
    mov ah,1
    int 80h
    jmp back
done:
    mov dx,OFFSET crlf
    mov ah,2
    int 80h
    xor ah,ah
    int 80h

ask  db 'Type a line: ',0
crlf db 13,10,0
line db 40 dup (0)
CODE ENDS
END start
```

## Pitfalls

* **Falling off the end.** Without the exit call, execution runs into your
  data and crashes. Always finish with `xor ah,ah` / `int 80h` or `ret`.
* **Data in the way.** Execution starts at `100h`. If you put data first,
  start with `jmp start`.
* **`AH` is the function number.** `mov ah,1` clobbers the high half of `AX`;
  load `AL` *after* `AH` if both matter, or use `mov al,'x'` / `mov ah,1`.
* **Forgetting `OFFSET`.** `mov dx,msg` loads the first two bytes of the
  string, not its address.
* **Missing NUL.** `AH=2` prints until it sees a `0` byte.
* **Unbalanced stack.** A `ret` to exit only works if `SP` is back where it
  started; use `AH=0` when in doubt.
* **No `INT 21h`.** Code written for DOS will not run: it expects services
  that do not exist here. Port the I/O to `INT 80h`.
* **Tight loops.** Nothing preempts your program. Long computations freeze
  the desktop until they finish.
* **Do not poke the video memory or mode.** See the memory map above.

## Checklist for an agent

1. Create `src/progs/NAME.ASM` starting from the quick-start template; keep
   `ORG 100h` and the `CODE SEGMENT ... END start` frame.
2. Use only 8086 instructions and the four `INT 80h` functions.
3. End every path with `xor ah,ah` / `int 80h`.
4. Assemble: `python scripts/wkasm.py src/progs/NAME.ASM -o out/NAME.WKX`
   and fix any error it reports (it names the line).
5. Build the image: `python scripts/mkimage.py`.
6. Verify headlessly with the snippet in
   [Testing without a screen](#testing-without-a-screen), and run
   `python scripts/test.py` to make sure nothing else broke.
7. File names are 8.3 (`NAME` at most 8 characters).

## For kernel contributors: adding a system call

The handler is `sys_call` in `src/TERM.INC`. It saves all registers, points
`DS`/`ES` at the kernel, and dispatches on the caller's `AH` (read from the
saved copy at `[bp+16]`; write `[bp+16]` to return a value in `AX`). Add a
`cmp ah,N` / `je` pair and a handler that ends by jumping to `sys_back`, then
document it in the table above.
