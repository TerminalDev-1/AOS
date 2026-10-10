"""Self-test for the assembler: known 8086 encodings and error handling.

    python scripts/test_wkasm.py

Each case is a source snippet and the exact bytes it must produce. The
encodings are the ones MASM 5.10 emits, and the whole WinKit kernel,
boot sector and programs were verified byte for byte against MASM's output
when the assembler was written.
"""
import os
import sys
import tempfile

import wkasm

CASES = [
    ("mov ax,bx", "8BC3"),
    ("mov al,5", "B005"),
    ("mov bx,1234h", "BB3412"),
    ("mov [bx+si+4],cx", "894804"),
    ("mov cx,[bp+8]", "8B4E08"),
    ("mov ax,[bp]", "8B4600"),
    ("mov ax,[1234h]", "A13412"),
    ("mov [1234h],al", "A23412"),
    ("mov es:[di],al", "268805"),
    ("mov byte ptr [si],7", "C60407"),
    ("mov word ptr [bx],7", "C7070700"),
    ("mov ds,ax", "8ED8"),
    ("mov ax,cs", "8CC8"),
    ("xor ax,ax", "33C0"),
    ("add ax,5", "050500"),
    ("add bx,5", "83C305"),
    ("add bx,1000h", "81C30010"),
    ("sub al,1", "2C01"),
    ("cmp al,'0'", "3C30"),
    ("and ax,0fff8h", "25F8FF"),
    ("or byte ptr [di],80h", "800D80"),
    ("test al,10h", "A810"),
    ("test ax,ax", "85C0"),
    ("inc cx", "41"),
    ("dec byte ptr [si]", "FE0C"),
    ("neg bx", "F7DB"),
    ("mul bx", "F7E3"),
    ("div bl", "F6F3"),
    ("shl ax,cl", "D3E0"),
    ("shr bx,1", "D1EB"),
    ("push es", "06"),
    ("pop ds", "1F"),
    ("push ax", "50"),
    ("push word ptr [bx]", "FF37"),
    ("int 10h", "CD10"),
    ("out dx,ax", "EF"),
    ("in al,dx", "EC"),
    ("rep stosb", "F3AA"),
    ("repe cmpsb", "F3A6"),
    ("lea dx,[bp+di+6]", "8D5306"),
    ("xchg ax,cx", "91"),
    ("cli\nsti\ncld\nhlt", "FAFBFCF4"),
    ("x: jmp x", "EBFE"),
    ("jmp y\ny: nop", "EB019090"),
    ("x: nop\nje x", "9074FD"),
    ("call z\nz: ret", "E80000" + "C3"),
    ("v db 1,2\nv2 dw 300h\nmov al,v", "01020003A00000"),
    ("t db 'ab',0\ndb 3 dup (7)", "616200070707"),
    ("k equ 6\nmov al,k+1", "B007"),
    ("org 100h\nmov bx,offset l\nl: nop", "BB0301" + "90"),
    ("m macro a\nmov ax,a\nendm\nm 5", "B80500"),
]

ERRORS = [
    ("push 5", "cannot push"),
    ("shl ax,2", "count of 1 or CL"),
    ("je far_away\n" + "nop\n" * 200 + "far_away: nop", "out of range"),
    ("mov ax,undefined_name", "undefined"),
    ("mov [bx],5", "ambiguous"),
]


def assemble(text):
    with tempfile.TemporaryDirectory() as d:
        path = os.path.join(d, "t.asm")
        with open(path, "w") as fh:
            fh.write(text + "\n")
        return wkasm.assemble_file(path)[0]


def main():
    failures = 0
    for source, expect in CASES:
        want = bytes.fromhex(expect)
        try:
            got = assemble(source)
        except wkasm.AsmError as e:
            got = f"error: {e}".encode()
        if got != want:
            failures += 1
            print(f"FAIL  {source!r}: want {want.hex()} got {got!r}")
    for source, needle in ERRORS:
        try:
            assemble(source)
        except wkasm.AsmError as e:
            if needle not in str(e):
                failures += 1
                print(f"FAIL  {source!r}: wrong error {e}")
        else:
            failures += 1
            print(f"FAIL  {source!r}: expected an error containing {needle!r}")
    total = len(CASES) + len(ERRORS)
    print(f"{total - failures}/{total} assembler checks passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
