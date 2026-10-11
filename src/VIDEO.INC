; VIDEO.INC - the QEMU display driver.
;
; WinKit draws in VGA mode 12h: 640 x 480, 16 colours, four bit planes.
; A real monitor needs exactly that timing, but QEMU has no monitor: it
; takes the size of the picture from the VGA's own registers, whatever they
; say. So under QEMU this driver widens mode 12h to 848 x 480, which is
; 16:9, by changing two CRT controller registers: the number of displayed
; character columns and the length of a row in video memory. The desktop
; then fills a widescreen emulator window instead of a 4:3 rectangle.
;
; Everything else about the mode is untouched, so the renderer only has to
; know the row width: 848 x 480 is 106 bytes x 480 rows = 50,880 bytes a
; plane, which still fits a VGA plane (64 KB).
;
; QEMU is recognised by the "Bochs VBE" ID register of its video card (index
; port 1CEh, data port 1CFh). That interface is only used to recognise the
; card: its own 16-colour modes cannot be drawn in from a guest. On any
; other video card nothing is changed and the screen stays 640 x 480. The
; screen size is therefore not a constant: it is in scrw / rowb and the
; values derived from them (DATA.INC).

VBE_ID     EQU 0                       ; Bochs VBE register number
CRTC       EQU 3d4h                    ; CRT controller: index, then data
WIDE_W     EQU 848                     ; a multiple of 16
WIDE_ROWB  EQU WIDE_W/8

; AX = Bochs VBE register BX.
vbe_in PROC NEAR
    push dx
    mov dx,1ceh
    mov ax,bx
    out dx,ax
    inc dx
    in ax,dx
    pop dx
    ret
vbe_in ENDP

; Called once, right after mode 12h is set. Widens the screen under QEMU
; and records the screen size either way.
video_init PROC NEAR
    mov bx,VBE_ID
    call vbe_in
    and ax,0fff0h                      ; B0C0h..B0C5h, by version
    cmp ax,0b0c0h
    L_JNE vi_done
    mov dx,CRTC
    mov al,11h                         ; registers 0-7 are write-protected
    out dx,al
    inc dx
    in al,dx
    and al,7fh
    out dx,al
    dec dx
    mov ax,(WIDE_ROWB-1)*256+01h       ; columns displayed, minus one
    out dx,ax
    mov ax,(WIDE_ROWB/2)*256+13h       ; row length in memory, in words
    out dx,ax
    mov wide,1
    mov scrw,WIDE_W
    mov maxx,WIDE_W-1
    mov rowb,WIDE_ROWB
    mov lastcol,WIDE_ROWB-1
    mov rowskip,WIDE_ROWB-WIN_W/8
    mov backofs,(WIN_H-1)*WIDE_ROWB+(WIN_W/8-1)
    mov wxmax,WIDE_W-WIN_W
    mov dlgx,(WIDE_W-640)/2
    mov guix,(WIDE_W-WIN_W)/2
    mov clockx,WIDE_W-92
    mov clx1,WIDE_W
    mov sy_b1+4,'8'                    ; "VGA 640 x 480" in System Properties
    mov sy_b1+5,'4'
    mov sy_b1+6,'8'
vi_done:
    ret
video_init ENDP
