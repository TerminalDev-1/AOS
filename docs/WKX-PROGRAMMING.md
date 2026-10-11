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
python scripts/run.py --snapshot   # boot WinKit; after the boot screen press Enter, open Terminal (F6)
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
| `A000:0000` | VGA video memory (mode 12h; rows are 80 bytes, or 106 on QEMU's wide screen) | do not touch |

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
| `04h` | save file | `DS:DX` -> NUL-terminated name, `DS:SI` -> data, `CX` = bytes | `AX` = 0 saved, 1 failed (clobbered) |
| `05h` | run another program | `DS:DX` -> NUL-terminated file name | returns only on failure, with `AX` = 1 |
| `06h` | read file | `DS:DX` -> NUL-terminated name, `DS:DI` -> buffer, `CX` = its size | `AX` = bytes read, or `FFFFh` if there is no such file |
| `10h` | open window | `DS:DX` -> NUL-terminated title | |
| `11h` | fill rectangle | `BX`,`CX` = x,y; `SI`,`DI` = width,height; `AL` = colour | |
| `12h` | draw text | `BX`,`CX` = x,y; `AL` = colour; `DS:DX` -> NUL-terminated string | |
| `13h` | draw 3D box | `BX`,`CX` = x,y; `SI`,`DI` = width,height; `AL` = bevel style | |
| `14h` | wait for event | | `AX` = event, `BX`, `CX` = its details (all three clobbered) |
| `15h` | poll for event | | as `14h`, but `AX` = 0 if nothing happened within one timer tick |

Any other `AH` is ignored and returns immediately.

### Output model

Output goes to the terminal screen, a **50 column x 16 row** text grid in the
Terminal window.

* `AH=1` / `AH=2` understand `13` (carriage return), `10` (line feed, also
  scrolls) and `8` (backspace). Use `13,10` for a new line.
* Other bytes below 32 are ignored (there is no tab). Bytes 32-255 are drawn
  with the BIOS 8x16 font.
* Text wraps at column 50 and the screen scrolls when it runs past row 16.
* The grid is redrawn after every call, but only while the Terminal window is
  the active, visible window.

### Input model

* `AH=3` **blocks** until a key is pressed, then returns the BIOS key code
  (`AL` = ASCII, `0` for extended keys such as the function keys;
  `AH` = scan code). There is no non-blocking read and no echo: print it yourself.
* The mouse reaches a program only as click events in its own window
  (`AH=14h`, below). The desktop is never delivered to programs.

### Saving a file

`AH=4` writes `CX` bytes from `DS:SI` to the disk as one whole file,
replacing any file of that name. The name is an 8.3 name such as
`'NOTE.TXT',0` (at most 23 characters are read) and `CX` may be up to 53,248. It
fails (`AX` = 1) when the name is empty, the size is too large, the disk or
the root directory is full, or the disk cannot be written.

### Reading a file

`AH=6` copies the file named at `DS:DX` to the buffer at `DS:DI`, at most
`CX` bytes of it, and returns the number of bytes copied in `AX`. A file
longer than the buffer is cut short without a warning, so give a buffer as
large as the file can be. `AX` = `FFFFh` means there is no such file.

Saving and reading whole files are the only file services: a program
cannot list, append to, rename or delete files. (The user can copy, rename
and delete files in the File Manager or the terminal.)

### Running another program

`AH=5` ends the calling program and starts the one named at `DS:DX` in its
place (a full file name such as `'CLOCK.WKX',0`). Nothing is passed to it and
control never comes back to the caller. If there is no such file the call
returns with `AX` = 1 and the caller carries on. The Store uses this for
its Run button.

### A window of your own

A program can be a graphical one: `AH=10h` opens a window on top of the
desktop, and the program draws in it and waits for events.

* **One window, fixed.** It is 416 x 288 pixels, centred on the screen
  (which is 640 or 848 pixels wide; a program never needs to know), with the
  title you pass (at most 40 characters) and a close button. It cannot be
  moved, resized or minimized, and a program cannot have a second one.
  Calling `AH=10h` again wipes the window, which is the way to clear it.
* **Client coordinates.** `(0,0)` is the top-left pixel below the title
  bar. The client area is **410 pixels wide and 264 high**. Drawing is
  clipped to the window, so stray coordinates cannot damage the desktop,
  but keep x and y inside the client area: negative values are not
  supported.
* **Colours** are WinKit's 16 palette entries: 0 black, 1 navy, 2 sky,
  3 teal, 4 face (the silver window colour), 5 shadow grey, 6 white,
  7 light grey, 8 pale grey, 9 red, 10 yellow, 11 green, 12 dark blue,
  13 blue, 14 orange, 15 cyan. A new window is filled with colour 4.
* **Text** (`AH=12h`) uses the 8 x 16 font: a character is 8 pixels wide,
  so the window holds 51 characters across. Only the foreground is drawn;
  to replace text, fill a rectangle over the old text first. At most 62
  characters are drawn per call.
* **3D boxes** (`AH=13h`) are the desktop's own bevels, filled: style
  0 raised button, 1 raised window frame, 2 sunken white field, 3 sunken
  one-pixel status panel, 4 pressed button. Make them at least 4 pixels
  wide and high. A button is a style 0 box with text drawn on it; the
  system has no button objects, so hit-test clicks yourself.
* **Events** (`AH=14h`) block until something happens and return it in
  `AX`: **1** a key, with `BL` = ASCII and `BH` = BIOS scan code (arrow
  keys come through: Up `48h`, Down `50h`, Left `4Bh`, Right `4Dh`);
  **2** a left-button press inside the client area, with `BX`,`CX` = x,y;
  **3** a click on the close button. The system does not close the window
  for you: exit when you get event 3. Without a window `AX` is 0.
* **Polling** (`AH=15h`) is `AH=14h` that gives up after one timer tick
  (about 55 ms) and returns `AX` = 0. Call it in a loop to animate or to
  keep a display current; `store/CLOCK.ASM` redraws the time this way.
* **The pointer** is visible and moves only while the program is waiting
  in `AH=14h` or `AH=15h`. Draw first, then wait.
* **Exit closes the window.** When the program exits, the window goes away
  and the desktop is repainted. There is no call to close it earlier.
* While the window is open the terminal is not redrawn, so `AH=1`/`AH=2`
  output only shows up after the program has exited. `AH=3` still reads
  keys, but use `AH=14h` in a windowed program.

A minimal windowed program (`src/progs/WINDOW.ASM`):

```asm
; WINDOW.WKX - the minimum windowed program: opens a window with one
; button and waits until the button or the close box is clicked.
CODE SEGMENT PARA PUBLIC 'CODE'
ASSUME CS:CODE, DS:CODE
ORG 100h

start:
    mov dx,OFFSET caption
    mov ah,10h                         ; open the window
    int 80h
    mov bx,20                          ; a raised button at 20,20, 96 x 24
    mov cx,20
    mov si,96
    mov di,24
    mov al,0
    mov ah,13h
    int 80h
    mov bx,52                          ; its label, in black
    mov cx,24
    mov dx,OFFSET quit
    mov al,0
    mov ah,12h
    int 80h
again:
    mov ah,14h                         ; AX = event, BX,CX = details
    int 80h
    cmp ax,3                           ; the close button
    je done
    cmp ax,2                           ; a click: on our button?
    jne again
    sub bx,20
    cmp bx,96
    jae again
    sub cx,20
    cmp cx,24
    jae again
done:
    mov dx,OFFSET bye                  ; shows in the terminal afterwards
    mov ah,2
    int 80h
    xor ah,ah                          ; exit: the window closes
    int 80h

caption db 'Hello window',0
quit    db 'Quit',0
bye     db 'The window is closed.',13,10,0
CODE ENDS
END start
```

A program is started from Start > Programs (every `.WKX` on the disk is
listed there, so yours appears as soon as it is on the disk), from its
desktop shortcut if it was installed after the image was built, by
double-clicking it in the File Manager, by name in the terminal, or by
another program with `AH=5`. The Store (`WKSTORE.WKX`) is
the one program that also has a Start menu entry, a desktop icon and a key
(F7). A program started from the desktop that prints with `AH=1`/`AH=2` or
reads a key with `AH=3` gets the terminal window opened for it.

### Things the system does not provide

No directory listing, no graphics outside that one window, no bitmaps, no timer, no memory allocator, no arguments, no DOS
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
* While a program runs, the desktop is frozen: the other windows do not
  respond, the clock does not update, and the pointer only moves while the
  program waits in `AH=14h` or `AH=15h`.

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

Inside WinKit: wait for the boot screen to finish, press Enter on the
welcome dialog, open the terminal with F6
or Start > Terminal, then type the program's name (the `.WKX` is optional).
Names are 8.3 and case-insensitive. `dir` lists what is on the disk.

## Testing without a screen

`scripts/test.py` drives WinKit in a headless QEMU and reads the terminal
back out of guest memory. Reuse that to test your program:

```python
import sys, time
sys.path.insert(0, "scripts")
from qemu import Machine            # scripts/qemu.py

with Machine("out/winkit.img") as m:
    m.wait_desktop()                # the boot screen is over
    m.key("ret")                    # leave the welcome dialog
    m.key("f6")                     # open Terminal
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
| `WINDOW.ASM` | the minimum windowed program: a window, a button, click and close events |
| `SETUP.ASM` | the hard disk installer: a windowed program doing raw sector I/O with BIOS `INT 13h`, with a progress bar |
| `WKSTORE.ASM` | the WinKit Store: a windowed program (buttons, a list, clicks and keys), polling the serial port, `INT 1Ah` timeouts, `AH=4` to save a file |

The programs offered by the WinKit Store are in `store/`: `CLOCK.ASM`
(polling with `AH=15h`), `DICE.ASM` (drawing with rectangles) and
`GUESS.ASM` (a text field and buttons). They are windowed `.WKX` sources
that are not put on the disk by `mkimage.py`, and the Store only accepts
windowed programs; see "The WinKit Store" in
[BUILDING.md](../BUILDING.md) for how to publish one.

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
2. Use only 8086 instructions and the `INT 80h` functions in the table above.
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
