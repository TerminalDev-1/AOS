; WinKit boot sector. The BIOS loads this at 0000:7C00; it reads the
; kernel image from the reserved sectors that follow (LBA 1..ksects) to
; 1000:0100 and jumps to it. The FAT12 volume starts after those sectors.
; mkimage.py patches rsvd and ksects. Assembled as a flat binary (ORG 0).
;
; The disk's shape (sectors per track, heads) is in the BPB below, and that
; is right for a floppy. When the BIOS starts this as a hard disk (a hard
; disk after Setup, or the image written to a USB stick) the shape is
; whatever the BIOS made up, and it cannot always even say what that is
; (a BIOS may use more sectors per track than the 63 it can report). So a
; hard disk is read by sector number with the INT 13h extensions (AH=42h)
; when the BIOS has them, which every BIOS since the mid 1990s does; only
; without them is the BIOS asked for the shape. The kernel reads what was
; decided here from the BPB in memory: 0000:7C18 and lbamode at 0000:7C25.
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
lbamode db 0                           ; 1: read by sector number (7C25)
        db 29h
volid   dd 19881130h
label_  db 'WINKIT     '
fstype  db 'FAT12   '
ksects  dw 0                           ; patched: kernel size in sectors
lba     dw 1
left    dw 0
dest    dw 0100h
packet  db 10h,0                       ; INT 13h AH=42h: one sector
        dw 1
pk_off  dw 0
        dw 1000h
pk_lba  dw 0,0,0,0

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
    test dl,80h                        ; a hard disk?
    jz shaped
    mov ah,41h                         ; INT 13h extensions present?
    mov bx,55aah
    int 13h
    jc ask
    cmp bx,0aa55h
    jne ask
    test cl,1
    jz ask
    mov byte ptr [lbamode],1
    jmp shaped
ask:
    mov dl,[drvnum]                    ; no: ask for its shape
    mov ah,8
    int 13h
    jc shaped
    and cx,63                          ; sectors per track
    jz shaped
    mov [spt],cx
    mov dl,dh                          ; last head number
    xor dh,dh
    inc dx
    mov [heads],dx
shaped:
    mov ax,1000h
    mov es,ax
    mov ax,[ksects]
    mov [left],ax
load:
    mov di,3
retry:
    cmp byte ptr [lbamode],0
    je by_chs
    mov ax,[lba]
    mov [pk_lba],ax
    mov ax,[dest]
    mov [pk_off],ax
    mov si,OFFSET packet
    mov dl,[drvnum]
    mov ah,42h
    int 13h
    jmp was_read
by_chs:
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
was_read:
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
