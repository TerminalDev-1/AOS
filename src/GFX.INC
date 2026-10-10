; GFX.INC - VGA mode 12h renderer. Everything is in pixel coordinates.
; Planes are written through the set/reset + bit-mask path: set/reset holds
; the colour, the bit mask selects pixels, the latches supply the rest.
; Every primitive is clipped to clx0..clx1 / cly0..cly1 (x in multiples of 8).

; Palette: identity attribute map, then our own DAC colours.
set_palette PROC NEAR
    push es
    push cs
    pop es
    mov dx,OFFSET attrmap
    mov ax,1002h
    int 10h
    mov dx,OFFSET dacrgb
    xor bx,bx
    mov cx,16
    mov ax,1012h
    int 10h
    pop es
    ret
set_palette ENDP

; Write one masked byte column: BL = pixel mask, DI = top byte, DX = 3CEh.
rcol PROC NEAR
    mov ah,bl
    mov al,8
    out dx,ax
    mov cx,rrows
rcol_row:
    mov al,es:[di]                     ; load latches for unmasked pixels
    mov es:[di],al
    add di,80
    loop rcol_row
    ret
rcol ENDP

; Solid rectangle rx/ry/rw/rh in colour ink. Preserves every register.
rect PROC NEAR
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax,rx                          ; horizontal clip
    cmp ax,clx0
    jae rc_a
    mov ax,clx0
rc_a:
    mov bx,rx
    add bx,rw
    cmp bx,clx1
    jbe rc_b
    mov bx,clx1
rc_b:
    cmp ax,bx
    L_JAE rc_exit
    mov rcx0,ax
    mov rcx1,bx
    mov ax,ry                          ; vertical clip
    cmp ax,cly0
    jae rc_c
    mov ax,cly0
rc_c:
    mov bx,ry
    add bx,rh
    cmp bx,cly1
    jbe rc_d
    mov bx,cly1
rc_d:
    cmp ax,bx
    L_JAE rc_exit
    sub bx,ax
    mov rrows,bx
    mov bx,80
    mul bx
    mov di,ax                          ; di = first row offset
    mov cl,3
    mov ax,rcx0
    shr ax,cl
    mov si,ax                          ; si = left byte
    mov ax,rcx1
    dec ax
    shr ax,cl
    mov rrb,ax                         ; right byte
    add di,si
    mov ax,rcx0                        ; left edge mask: FF >> (x0 & 7)
    and al,7
    mov cl,al
    mov al,0ffh
    shr al,cl
    mov rlm,al
    mov ax,rcx1                        ; right edge mask: FF << (7 - ((x1-1)&7))
    dec ax
    and al,7
    mov cl,7
    sub cl,al
    mov al,0ffh
    shl al,cl
    mov rrm,al
    mov ax,0a000h
    mov es,ax
    mov dx,3ceh
    mov ah,ink
    xor al,al
    out dx,ax                          ; set/reset value = colour
    mov ax,rrb
    cmp ax,si
    jne rc_multi
    mov bl,rlm                         ; within a single byte
    and bl,rrm
    call rcol
    jmp rc_end
rc_multi:
    mov rcnt,ax
    sub rcnt,si
    inc rcnt                           ; bytes touched
    cmp rlm,0ffh
    je rc_noleft
    mov bl,rlm
    push di
    call rcol
    pop di
    inc di
    dec rcnt
rc_noleft:
    cmp rrm,0ffh
    je rc_noright
    mov bl,rrm
    push di
    add di,rcnt
    dec di
    call rcol
    pop di
    dec rcnt
rc_noright:
    cmp rcnt,0
    je rc_end
    mov ax,0ff08h
    out dx,ax
    mov bx,rrows
rc_span:
    push di
    mov cx,rcnt
    xor al,al
    rep stosb
    pop di
    add di,80
    dec bx
    jnz rc_span
rc_end:
    mov dx,3ceh
    mov ax,0ff08h
    out dx,ax
rc_exit:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
rect ENDP

; NUL string DS:SI at pixel tx/ty, colour fg, BIOS 8x16 font.
textout PROC NEAR
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov ax,cly0                        ; visible glyph rows tr0..tr0+trn
    sub ax,ty
    jg to_a
    xor ax,ax
to_a:
    mov tr0,ax
    mov ax,cly1
    sub ax,ty
    cmp ax,16
    jl to_b
    mov ax,16
to_b:
    sub ax,tr0
    L_JLE to_done
    mov trn,ax
    mov cl,3                           ; clip box in byte columns
    mov ax,clx0
    shr ax,cl
    mov tclb0,ax
    mov ax,clx1
    add ax,7
    shr ax,cl
    mov tclb1,ax
    mov ax,tx
    mov tpx,ax
    mov ax,ty
    add ax,tr0
    mov bx,80
    mul bx
    mov trow,ax
    mov ax,0a000h
    mov es,ax
    mov dx,3ceh
    mov ah,fg
    xor al,al
    out dx,ax
to_char:
    lodsb
    or al,al
    L_JZ to_done
    xor ah,ah
    mov cl,4
    shl ax,cl
    add ax,fontoff
    add ax,tr0
    mov glyph,ax
    mov bx,tpx
    add tpx,8
    mov ax,bx
    and al,7
    mov sft,al                         ; pixel offset inside the byte
    mov cl,3
    shr bx,cl                          ; bx = byte column
    mov ok0,0
    mov ok1,0
    cmp bx,tclb0
    jb to_f0
    cmp bx,tclb1
    jae to_f0
    mov ok0,1
to_f0:
    cmp sft,0
    je to_f1
    mov ax,bx
    inc ax
    cmp ax,tclb0
    jb to_f1
    cmp ax,tclb1
    jae to_f1
    mov ok1,1
to_f1:
    mov al,ok0
    or al,ok1
    L_JZ to_char
    mov di,trow
    add di,bx
    mov bp,trn
to_row:
    push ds
    mov ds,fontseg
    mov bx,cs:glyph
    mov ah,[bx]
    pop ds
    xor al,al
    mov cl,sft
    shr ax,cl                          ; ah = bits in first byte, al = spill
    mov bl,al
    cmp ok0,0
    je to_w1
    mov al,8
    out dx,ax
    mov al,es:[di]
    mov es:[di],al
to_w1:
    cmp ok1,0
    je to_next
    mov ah,bl
    mov al,8
    out dx,ax
    mov al,es:[di+1]
    mov es:[di+1],al
to_next:
    inc glyph
    add di,80
    dec bp
    jnz to_row
    jmp to_char
to_done:
    mov dx,3ceh
    mov ax,0ff08h
    out dx,ax
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
textout ENDP

; SI = string -> CX = length. SI preserved.
strlen PROC NEAR
    push ax
    push si
    xor cx,cx
sl_next:
    lodsb
    or al,al
    jz sl_done
    inc cx
    jmp sl_next
sl_done:
    pop si
    pop ax
    ret
strlen ENDP

; Text centred on pixel column ctrx.
textc PROC NEAR
    push cx
    call strlen
    shl cx,1
    shl cx,1                           ; half the width: 4 px per char
    mov ax,ctrx
    sub ax,cx
    mov tx,ax
    call textout
    pop cx
    ret
textc ENDP

; Font scaled up bsc times: one bsc x bsc block per set font pixel.
bigtext PROC NEAR
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al,fg
    mov ink,al
    mov ax,tx
    mov bcx,ax
bt_char:
    lodsb
    or al,al
    jz bt_done
    xor ah,ah
    mov cl,4
    shl ax,cl
    add ax,fontoff
    mov bgl,ax
    mov ax,ty
    mov bcy,ax
    mov di,16
bt_row:
    push ds
    mov ds,fontseg
    mov bx,cs:bgl
    mov al,[bx]
    pop ds
    mov bmk,al
    mov ax,bcx
    mov cx,8
bt_bit:
    shl bmk,1
    jnc bt_skip
    mov rx,ax
    mov bx,bcy
    mov ry,bx
    mov bx,bsc
    mov rw,bx
    mov rh,bx
    call rect
bt_skip:
    add ax,bsc
    loop bt_bit
    inc bgl
    mov bx,bsc
    add bcy,bx
    dec di
    jnz bt_row
    mov ax,bsc
    shl ax,1
    shl ax,1
    shl ax,1
    add bcx,ax
    jmp bt_char
bt_done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
bigtext ENDP

; Text rotated 90 degrees counter-clockwise (reads bottom to top): the
; first glyph's lower-left corner sits at tx/ty. One pixel per set bit.
rottext PROC NEAR
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al,fg
    mov ink,al
    mov ax,tx
    mov rtx,ax
    mov ax,ty
    mov rty,ax
    mov rw,1
    mov rh,1
rt_char:
    lodsb
    or al,al
    jz rt_done
    xor ah,ah
    mov cl,4
    shl ax,cl
    add ax,fontoff
    mov bgl,ax
    xor di,di                          ; glyph row 0..15
rt_row:
    push ds
    mov ds,fontseg
    mov bx,cs:bgl
    mov al,[bx]
    pop ds
    mov bmk,al
    mov ax,rtx
    add ax,di
    mov rx,ax                          ; glyph row -> x
    mov ax,rty
    mov cx,8
rt_bit:
    shl bmk,1
    jnc rt_skip
    mov ry,ax                          ; glyph column -> upward y
    call rect
rt_skip:
    dec ax
    loop rt_bit
    inc bgl
    inc di
    cmp di,16
    jb rt_row
    sub rty,8
    jmp rt_char
rt_done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
rottext ENDP

; ---- pointer: pixel-accurate arrow with saved background ----

; Snapshot the pointer, save the pixels under it, then draw the arrow.
cursor_show PROC NEAR
    cli
    mov ax,mx
    mov curx,ax
    mov ax,my
    mov cury,ax
    sti
    call save_under
    mov si,OFFSET cursorblack
    mov ah,K_BLACK
    call cursor_mask
    mov si,OFFSET cursorwhite
    mov ah,K_WHITE
    call cursor_mask
    ret
cursor_show ENDP

cursor_move PROC NEAR
    call restore_under
    call cursor_show
    ret
cursor_move ENDP

; Draw the 16x16 mask at SI in colour AH at curx/cury (3 bytes per row).
cursor_mask PROC NEAR
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    mov dx,3ceh
    xor al,al
    out dx,ax                          ; set/reset value = colour
    mov ax,cury
    mov bx,80
    mul bx
    mov di,ax
    mov bx,curx
    mov ax,bx
    and al,7
    mov csft,al
    mov cl,3
    shr bx,cl
    mov ccol,bx
    add di,bx
    mov ax,0a000h
    mov es,ax
    mov dx,3ceh
    mov cx,16
cm_row:
    push cx
    mov ah,[si]
    mov al,[si+1]
    add si,2
    xor bl,bl
    mov bh,csft
cm_shift:
    cmp bh,0
    je cm_shifted
    shr ah,1
    rcr al,1
    rcr bl,1
    dec bh
    jmp cm_shift
cm_shifted:
    mov bh,al
    mov al,8
    out dx,ax                          ; first byte
    mov al,es:[di]
    mov es:[di],al
    cmp ccol,79
    jae cm_next
    mov ah,bh
    mov al,8
    out dx,ax                          ; second byte
    mov al,es:[di+1]
    mov es:[di+1],al
    cmp ccol,78
    jae cm_next
    mov ah,bl
    mov al,8
    out dx,ax                          ; third byte
    mov al,es:[di+2]
    mov es:[di+2],al
cm_next:
    add di,80
    pop cx
    dec cx
    L_JNZ cm_row
    mov ax,0ff08h
    out dx,ax
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret
cursor_mask ENDP

; Copy the 24 x 16 pixel area under the pointer from all four planes.
save_under PROC NEAR
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax,0a000h
    mov es,ax
    mov ax,cury
    mov bx,80
    mul bx
    mov bx,curx
    mov cl,3
    shr bx,cl
    mov savedcol,bx
    add ax,bx
    mov savedaddr,ax
    mov si,ax
    mov di,OFFSET under
    xor bl,bl
su_plane:
    mov dx,3ceh
    mov ah,bl
    mov al,4                           ; read map select
    out dx,ax
    push si
    mov cx,16
su_row:
    mov al,es:[si]
    mov [di],al
    mov al,es:[si+1]
    mov [di+1],al
    mov al,es:[si+2]
    mov [di+2],al
    add di,3
    add si,80
    loop su_row
    pop si
    inc bl
    cmp bl,4
    L_JB su_plane
    mov dx,3ceh
    mov ax,0004h
    out dx,ax
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
save_under ENDP

; Put the saved pixels back with plain CPU writes, one plane at a time.
restore_under PROC NEAR
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax,0a000h
    mov es,ax
    mov dx,3ceh
    mov ax,0001h                       ; set/reset off: CPU data goes through
    out dx,ax
    mov ax,0ff08h
    out dx,ax
    mov si,OFFSET under
    mov di,savedaddr
    mov bl,1
ru_plane:
    mov dx,3c4h
    mov ah,bl
    mov al,2                           ; sequencer map mask
    out dx,ax
    push di
    mov cx,16
ru_row:
    mov al,[si]
    mov es:[di],al
    cmp savedcol,79
    jae ru_clip
    mov al,[si+1]
    mov es:[di+1],al
    cmp savedcol,78
    jae ru_clip
    mov al,[si+2]
    mov es:[di+2],al
ru_clip:
    add si,3
    add di,80
    loop ru_row
    pop di
    shl bl,1
    cmp bl,16
    L_JB ru_plane
    mov dx,3c4h
    mov ax,0f02h
    out dx,ax
    mov dx,3ceh
    mov ax,0f01h
    out dx,ax
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
restore_under ENDP

; ---- partial repaint and window blit ----

; AX = clamp(AX, BX, CX), all unsigned.
clampv PROC NEAR
    cmp ax,cx
    jbe cv_hi
    mov ax,cx
cv_hi:
    cmp ax,bx
    jae cv_done
    mov ax,bx
cv_done:
    ret
clampv ENDP

; Repaint the scene inside x = AX..CX-1, y = BX..DX-1 (pixels).
repaint_area PROC NEAR
    cmp ax,cx
    L_JAE ra_done
    cmp bx,dx
    L_JAE ra_done
    mov clx0,ax
    mov cly0,bx
    mov clx1,cx
    mov cly1,dx
    mov clipped,1
    call paint
    mov clipped,0
    mov clx0,0
    mov cly0,0
    mov clx1,SCR_W
    mov cly1,SCR_H
ra_done:
    ret
repaint_area ENDP

; The active window moved from oldx/oldy to wx/wy[active]. Its pixels are
; copied with the VGA latches (write mode 1); only what it uncovered is
; repainted, so nothing flashes.
blit_window PROC NEAR
    xor bx,bx
    mov bl,active
    dec bx
    shl bx,1
    mov ax,wx[bx]
    mov nwx,ax
    mov ax,wy[bx]
    mov nwy,ax
    call restore_under
    mov ax,oldy
    mov cx,80
    mul cx
    mov si,oldx
    mov cl,3
    shr si,cl
    add si,ax
    mov ax,nwy
    mov cx,80
    mul cx
    mov di,nwx
    mov cl,3
    shr di,cl
    add di,ax
    push ds
    mov ax,0a000h
    mov ds,ax
    mov es,ax
    mov dx,3ceh
    mov ax,0105h                       ; write mode 1: copy the latches
    out dx,ax
    cmp di,si
    jb bw_fwd
    add si,BACK_OFS                    ; moving down/right: copy backwards
    add di,BACK_OFS
    std
    mov bx,WIN_H
bw_back:
    mov cx,WIN_W/8
    rep movsb
    sub si,80-WIN_W/8
    sub di,80-WIN_W/8
    dec bx
    L_JNZ bw_back
    cld
    jmp bw_copied
bw_fwd:
    cld
    mov bx,WIN_H
bw_fwd_row:
    mov cx,WIN_W/8
    rep movsb
    add si,80-WIN_W/8
    add di,80-WIN_W/8
    dec bx
    L_JNZ bw_fwd_row
bw_copied:
    mov ax,0005h                       ; back to write mode 0
    out dx,ax
    pop ds
    ; Old window rectangle minus the new one.
    mov ax,oldx
    mov rox0,ax
    add ax,WIN_W
    mov rox1,ax
    mov ax,oldy
    mov roy0,ax
    add ax,WIN_H
    mov roy1,ax
    mov ax,nwx
    mov rbx0,ax
    add ax,WIN_W
    mov rbx1,ax
    mov ax,nwy
    mov rby0,ax
    add ax,WIN_H
    mov rby1,ax
    mov ax,rby0
    mov bx,roy0
    mov cx,roy1
    call clampv
    mov ryl,ax
    mov ax,rby1
    mov bx,roy0
    mov cx,roy1
    call clampv
    mov ryh,ax
    mov ax,rbx0
    mov bx,rox0
    mov cx,rox1
    call clampv
    mov rxl,ax
    mov ax,rbx1
    mov bx,rox0
    mov cx,rox1
    call clampv
    mov rxr,ax
    mov ax,rox0                        ; band above the new body
    mov cx,rox1
    mov bx,roy0
    mov dx,ryl
    call repaint_area
    mov ax,rox0                        ; band below
    mov cx,rox1
    mov bx,ryh
    mov dx,roy1
    call repaint_area
    mov ax,rox0                        ; band to the left
    mov cx,rxl
    mov bx,ryl
    mov dx,ryh
    call repaint_area
    mov ax,rxr                         ; band to the right
    mov cx,rox1
    mov bx,ryl
    mov dx,ryh
    call repaint_area
    call cursor_show
    ret
blit_window ENDP
