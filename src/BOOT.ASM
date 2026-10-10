; WinKit boot sector. The BIOS loads this at 0000:7C00; it reads the
; kernel image from the reserved sectors that follow (LBA 1..ksects) to
; 1000:0100 and jumps to it. The FAT12 volume starts after those sectors.
; mkimage.py patches rsvd and ksects. Assembled as a flat binary (ORG 0).
CODE SEGMENT PARA PUBLIC 'CODE'
ASSUME CS:CODE, DS:CODE

start:
    jmp short boot
    nop
oem     db 'WINKIT  '
bps     dw 512
spc     db 1
rsvd    dw 1                           ; patched: 1 + kernel sectors
nfat    db 2
rootent dw 224
totsec  dw 2880
media   db 0f0h
fatsz   dw 9
spt     dw 18
heads   dw 2
hidden  dd 0
tot32   dd 0
drvnum  db 0                           ; the kernel reads this at 0000:7C24
        db 0
        db 29h
volid   dd 19881130h
label_  db 'WINKIT     '
fstype  db 'FAT12   '
ksects  dw 0                           ; patched: kernel size in sectors
lba     dw 1
left    dw 0
dest    dw 0100h

boot:
    cli
    xor ax,ax
    mov ss,ax
    mov sp,7c00h
    sti
    db 0eah                            ; jmp 07C0:next, so offsets start at 0
    dw OFFSET next,07c0h
next:
    mov ax,cs
    mov ds,ax
    mov [drvnum],dl
    mov ax,1000h
    mov es,ax
    mov ax,[ksects]
    mov [left],ax
load:
    mov di,3
retry:
    mov ax,[lba]
    xor dx,dx
    div word ptr [spt]
    inc dl
    mov cl,dl                          ; sector 1..spt
    xor dx,dx
    div word ptr [heads]
    mov ch,al                          ; cylinder
    mov dh,dl                          ; head
    mov dl,[drvnum]
    mov bx,[dest]
    mov ax,0201h
    int 13h
    jnc loaded
    xor ax,ax                          ; reset the controller and retry
    int 13h
    dec di
    jnz retry
    jmp fail
loaded:
    add word ptr [dest],512
    inc word ptr [lba]
    dec word ptr [left]
    jnz load
    mov ax,1000h
    mov ds,ax
    mov es,ax
    cli
    mov ss,ax
    mov sp,0fffeh
    sti
    db 0eah                            ; jmp 1000:0100
    dw 0100h,1000h

fail:
    mov si,OFFSET msg
show:
    lodsb
    or al,al
    jz halt
    mov ah,0eh
    mov bx,7
    int 10h
    jmp show
halt:
    cli
    hlt
    jmp halt

msg db 'WinKit: cannot read kernel',0

    db 510-($-start) dup (0)
    dw 0aa55h

CODE ENDS
END start
