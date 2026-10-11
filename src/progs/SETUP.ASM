; SETUP.WKX - installs WinKit on the hard disk. It copies the disk WinKit
; was started from, sector by sector, to a hard disk and adjusts the boot
; sector to that disk, so the machine then starts WinKit from it.
; Everything on that hard disk is overwritten.
;
; Which hard disk: started from a floppy, the first one (BIOS drive 80h).
; Started from a hard disk or a USB stick, which the BIOS also numbers as
; a hard disk, the next one after it (81h); with no such disk there is
; nothing to install on. Hard disks are read and written by sector number
; (INT 13h AH=42h/43h) when the BIOS can, like the kernel does.
; When the copy is done the Install button becomes Restart, which starts
; the machine from the hard disk straight away.
;
; The hard disk gets the same layout as the floppy: boot sector, kernel,
; and the 1.44 MB FAT12 volume, starting at its first sector (there is no
; partition table). Disk access is BIOS INT 13h; the window is INT 80h.
CODE SEGMENT PARA PUBLIC 'CODE'
ASSUME CS:CODE, DS:CODE
ORG 100h

K_BLACK   EQU 0                        ; the WinKit palette
K_NAVY    EQU 1
K_FACE    EQU 4
K_RED     EQU 9
BV_BUTTON EQU 0                        ; bevel styles
BV_FIELD  EQU 2

BAR_W     EQU 360                      ; progress bar, in pixels
HARDDISK  EQU 80h                      ; BIOS number of the first hard disk

; Filled rectangle.
GBOX MACRO x,y,w,h,c
    mov bx,x
    mov cx,y
    mov si,w
    mov di,h
    mov al,c
    mov ah,11h
    int 80h
ENDM

; 3D box in bevel style m.
GBEV MACRO x,y,w,h,m
    mov bx,x
    mov cx,y
    mov si,w
    mov di,h
    mov al,m
    mov ah,13h
    int 80h
ENDM

; The string s at x,y in colour c.
GTXT MACRO x,y,c,s
    mov bx,x
    mov cx,y
    mov dx,OFFSET s
    mov al,c
    mov ah,12h
    int 80h
ENDM

start:
    call probe
    mov dx,OFFSET t_title
    mov ah,10h                         ; open the window
    int 80h
    GTXT 20,16,K_BLACK,t_head
    GTXT 21,16,K_BLACK,t_head
    GTXT 20,44,K_BLACK,t_line1
    GTXT 20,62,K_BLACK,t_line2
    GTXT 20,90,K_RED,t_warn
    GBEV 20,130,366,22,BV_FIELD
    GBEV 115,210,84,24,BV_BUTTON
    GTXT 129,214,K_BLACK,t_install
    GBEV 211,210,84,24,BV_BUTTON
    GTXT 233,214,K_BLACK,t_close
    call show_status
events:
    mov ah,14h                         ; wait for a key or a click
    int 80h
    cmp ax,3                           ; the close button
    je quit
    cmp ax,1
    jne not_key
    cmp bl,27
    je quit
    cmp bl,13
    je install
    jmp events
not_key:
    cmp ax,2
    jne events
    sub cx,210                         ; both buttons are at y 210..233
    cmp cx,24
    jae events
    cmp bx,115
    jb events
    cmp bx,199
    jb install
    cmp bx,211
    jb events
    cmp bx,295
    jae events
quit:
    xor ah,ah                          ; exit
    int 80h

; Install was pressed: ask once more, then copy. After a good copy the
; same button restarts from the hard disk.
install:
    cmp done,0
    jne boot_hd
    cmp can,0
    je events
    cmp armed,0
    jne in_go
    mov armed,1
    mov status,OFFSET t_sure
    call show_status
    jmp events
in_go:
    mov can,0
    call copy
    call show_status
    cmp status,OFFSET t_done
    jne in_events
    mov done,1
    GBEV 115,210,84,24,BV_BUTTON       ; Install becomes Restart
    GTXT 129,214,K_BLACK,t_restart
in_events:
    jmp events

; Start the machine from the hard disk, whatever the BIOS boot order is:
; read its boot sector to 0000:7C00, as the BIOS would, and jump to it with
; the drive number in DL. If that cannot be done, restart the usual way.
boot_hd:
    mov ax,0c200h                      ; PS/2 mouse off, and no callback
    xor bh,bh
    int 15h
    mov ax,0c207h
    xor bx,bx
    mov es,bx
    int 15h
    mov ax,3                           ; text mode, as after a power-on
    int 10h
    xor ax,ax
    mov es,ax
    mov di,3
bh_try:
    mov ax,0201h                       ; read one sector
    mov bx,7c00h
    mov cx,1                           ; cylinder 0, sector 1
    xor dh,dh                          ; head 0
    mov dl,dstdrv
    int 13h
    jnc bh_read
    xor ax,ax
    mov dl,dstdrv
    int 13h
    dec di
    jnz bh_try
    int 19h
bh_read:
    cmp word ptr es:[7dfeh],0aa55h     ; a boot sector?
    je bh_go
    int 19h
bh_go:
    cli
    xor ax,ax
    mov ss,ax
    mov sp,7c00h
    sti
    mov dl,dstdrv
    db 0eah                            ; jmp 0000:7C00
    dw 7c00h,0

; Look at both disks. Sets can = 1 and the opening status text when the
; hard disk can take WinKit.
probe:
    push es
    xor ax,ax
    mov es,ax                          ; the boot sector the BIOS loaded
    mov al,es:[7c24h]
    mov srcdrv,al
    mov ax,es:[7c18h]
    mov sspt,ax
    mov ax,es:[7c1ah]
    mov sheads,ax
    mov ax,es:[7c13h]
    mov total,ax
    mov al,es:[7c25h]
    mov slba,al
    pop es
    mov pk_seg,ds
    mov status,OFFSET t_fromhd
    cmp srcdrv,HARDDISK                ; started from a hard disk or stick:
    jb pr_which                        ; the target is the one after it
    mov al,srcdrv
    inc al
    mov dstdrv,al
    jmp pr_look
pr_which:
    mov status,OFFSET t_nodisk
pr_look:
    mov ah,41h                         ; can it be used by sector number?
    mov bx,55aah
    mov dl,dstdrv
    int 13h
    jc pr_shape
    cmp bx,0aa55h
    jne pr_shape
    test cl,1
    jz pr_shape
    mov dlba,1
pr_shape:
    push es
    mov ah,8                           ; hard disk geometry
    mov dl,dstdrv
    int 13h
    pop es
    jc pr_done
    mov al,dstdrv                      ; DL = number of hard disks
    sub al,HARDDISK
    cmp dl,al
    jbe pr_done
    mov al,cl
    and al,63
    xor ah,ah
    or ax,ax
    jz pr_done
    mov dspt,ax                        ; sectors per track
    mov al,dh
    xor ah,ah
    inc ax
    mov dheads,ax
    mov al,ch                          ; cylinders: 10 bits in CH and CL
    mov ah,cl
    mov cl,6
    shr ah,cl
    inc ax
    mov bx,ax
    mov ax,dspt
    mul dheads
    mul bx                             ; DX:AX = sectors on the disk
    mov status,OFFSET t_small
    cmp dlba,0                         ; its shape may not say its size
    jne pr_ok
    or dx,dx
    jnz pr_ok
    cmp ax,total
    jb pr_done
pr_ok:
    mov can,1
    mov status,OFFSET t_ready
pr_done:
    ret

; Copy every sector. Leaves the result in status.
copy:
    mov status,OFFSET t_copying
    call show_status
    GBEV 20,130,366,22,BV_FIELD        ; an empty bar
    mov ax,total                       ; sectors for each pixel of the bar
    xor dx,dx
    mov bx,BAR_W
    div bx
    or ax,ax
    jnz co_step
    inc ax
co_step:
    mov step,ax
    mov lba,0
co_next:
    mov al,srcdrv                      ; read from the WinKit disk
    mov g_drv,al
    mov ax,sspt
    mov g_spt,ax
    mov ax,sheads
    mov g_heads,ax
    mov al,slba
    mov g_lba,al
    mov g_op,2
    call disk_io
    jnc co_read
    mov status,OFFSET t_rerr
    ret
co_read:
    cmp lba,0
    jne co_write
    mov bx,OFFSET heap                 ; the boot sector: this disk's shape
    mov ax,dspt
    mov [bx+18h],ax
    mov ax,dheads
    mov [bx+1ah],ax
    mov al,dstdrv
    mov [bx+24h],al
co_write:
    mov al,dstdrv                      ; write to the hard disk
    mov g_drv,al
    mov al,dlba
    mov g_lba,al
    mov ax,dspt
    mov g_spt,ax
    mov ax,dheads
    mov g_heads,ax
    mov g_op,3
    call disk_io
    jnc co_wrote
    mov status,OFFSET t_werr
    ret
co_wrote:
    inc lba
    mov ax,lba
    test al,15                         ; move the bar every 16 sectors
    jnz co_more
    call bar
co_more:
    mov ax,lba
    cmp ax,total
    jae co_all
    jmp co_next
co_all:
    call bar
    mov status,OFFSET t_done
    ret

; The progress bar for lba sectors out of total.
bar:
    mov ax,lba
    xor dx,dx
    div step
    cmp ax,BAR_W
    jbe ba_draw
    mov ax,BAR_W
ba_draw:
    mov si,ax
    mov bx,23
    mov cx,133
    mov di,16
    mov al,K_NAVY
    mov ah,11h
    int 80h
    ret

; Reads (g_op = 2) or writes (3) sector lba of drive g_drv at heap: by
; sector number when g_lba is set, else through its shape g_spt / g_heads.
; CF set when it failed three times.
disk_io:
    cmp g_lba,0
    je di_chs
    mov di,3
di_ltry:
    mov ax,lba                         ; by sector number
    mov pk_lba,ax
    mov si,OFFSET packet
    mov dl,g_drv
    mov ah,g_op
    add ah,40h                         ; 42h read, 43h write
    xor al,al
    int 13h
    jnc di_ok
    xor ax,ax
    mov dl,g_drv
    int 13h
    dec di
    jnz di_ltry
    stc
    ret
di_chs:
    mov di,3
di_try:
    mov ax,lba
    xor dx,dx
    div g_spt                          ; DX = sector - 1
    mov cl,dl
    inc cl
    xor dx,dx
    div g_heads                        ; AX = cylinder, DX = head
    mov ch,al
    mov dh,dl
    mov al,ah                          ; cylinder bits 8-9 go in CL bits 6-7
    ror al,1
    ror al,1
    and al,0c0h
    or cl,al
    mov dl,g_drv
    mov bx,OFFSET heap
    mov ah,g_op
    mov al,1
    int 13h
    jnc di_ok
    xor ax,ax                          ; reset the drive and try again
    mov dl,g_drv
    int 13h
    dec di
    jnz di_try
    stc
    ret
di_ok:
    clc
    ret

; The line under the bar shows the string that status points to.
show_status:
    GBOX 20,164,380,16,K_FACE
    mov bx,20
    mov cx,164
    mov dx,status
    mov al,K_BLACK
    mov ah,12h
    int 80h
    ret

status  dw 0                           ; text for the status line
total   dw 0                           ; sectors to copy
lba     dw 0                           ; the sector being copied
step    dw 0
sspt    dw 0                           ; shape of the disk WinKit runs from
sheads  dw 0
dspt    dw 0                           ; shape of the hard disk
dheads  dw 0
g_spt   dw 0                           ; what disk_io works on
g_heads dw 0
g_drv   db 0
g_op    db 0
g_lba   db 0
srcdrv  db 0                           ; the drive WinKit was started from
dstdrv  db HARDDISK                    ; the hard disk to install on
slba    db 0                           ; each: use it by sector number
dlba    db 0
packet  db 10h,0                       ; INT 13h AH=42h/43h: one sector
        dw 1
        dw OFFSET heap
pk_seg  dw 0
pk_lba  dw 0,0,0,0
can     db 0                           ; installing is possible
armed   db 0                           ; Install was pressed once
done    db 0                           ; installed: the button restarts

t_title   db 'WinKit Setup',0
t_head    db 'Install WinKit on the hard disk',0
t_line1   db 'This copies the whole system from this disk to',0
t_line2   db 'the hard disk, so the PC starts WinKit from it.',0
t_warn    db 'Everything on the hard disk will be erased.',0
t_install db 'Install',0
t_close   db 'Close',0
t_restart db 'Restart',0
t_ready   db 'Press Install to begin.',0
t_sure    db 'Sure? Press Install again to erase and install.',0
t_copying db 'Copying...',0
t_done    db 'Done. Press Restart to boot the hard disk.',0
t_nodisk  db 'No hard disk was found.',0
t_small   db 'The hard disk is too small (1.44 MB needed).',0
t_fromhd  db 'Running from the hard disk; no other disk found.',0
t_rerr    db 'Cannot read the WinKit disk. Not installed.',0
t_werr    db 'Cannot write to the hard disk. Not installed.',0

; Not part of the file: one sector.
heap:
CODE ENDS
END start
