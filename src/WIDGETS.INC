; WIDGETS.INC - 3D bevels, title gradient, window chrome, icons, buttons.
; A button table drives both drawing and hit testing, so the two can never
; disagree. Entries are B_SIZE bytes; the table ends with x = 0FFFFh.

; ---- 3D bevel around bvx/bvy/bvw/bvh in style bvm ----
; outer top/left bc1, inner top/left bc2, outer bottom/right bc3,
; inner bottom/right bc4 (FF = absent), interior fill bcf.
bevel PROC NEAR
    push ax
    mov bins,2
    mov al,bvm
    cmp al,BV_BUTTON
    jne bv_1
    mov bc1,K_WHITE
    mov bc2,K_LIGHT
    mov bc3,K_BLACK
    mov bc4,K_SHADOW
    mov bcf,K_FACE
    jmp bv_go
bv_1:
    cmp al,BV_WINDOW
    jne bv_2
    mov bc1,K_LIGHT
    mov bc2,K_WHITE
    mov bc3,K_BLACK
    mov bc4,K_SHADOW
    mov bcf,K_FACE
    jmp bv_go
bv_2:
    cmp al,BV_FIELD
    jne bv_3
    mov bc1,K_SHADOW
    mov bc2,K_BLACK
    mov bc3,K_WHITE
    mov bc4,K_LIGHT
    mov bcf,K_WHITE
    jmp bv_go
bv_3:
    cmp al,BV_PRESS
    jne bv_4
    mov bc1,K_BLACK
    mov bc2,K_SHADOW
    mov bc3,K_WHITE
    mov bc4,K_LIGHT
    mov bcf,K_LIGHT
    jmp bv_go
bv_4:                                  ; BV_PANEL: one pixel, sunken
    mov bins,1
    mov bc1,K_SHADOW
    mov bc2,0ffh
    mov bc3,K_WHITE
    mov bc4,0ffh
    mov bcf,K_FACE
bv_go:
    mov ax,bvx                         ; interior
    add ax,bins
    mov rx,ax
    mov ax,bvy
    add ax,bins
    mov ry,ax
    mov ax,bvw
    sub ax,bins
    sub ax,bins
    mov rw,ax
    mov ax,bvh
    sub ax,bins
    sub ax,bins
    mov rh,ax
    mov al,bcf
    mov ink,al
    call rect
    mov al,bc1                         ; outer top and left
    mov ink,al
    mov ax,bvx
    mov rx,ax
    mov ax,bvy
    mov ry,ax
    mov ax,bvw
    mov rw,ax
    mov rh,1
    call rect
    mov rw,1
    mov ax,bvh
    mov rh,ax
    call rect
    mov al,bc3                         ; outer bottom and right
    mov ink,al
    mov ax,bvy
    add ax,bvh
    dec ax
    mov ry,ax
    mov ax,bvw
    mov rw,ax
    mov rh,1
    call rect
    mov ax,bvx
    add ax,bvw
    dec ax
    mov rx,ax
    mov ax,bvy
    mov ry,ax
    mov rw,1
    mov ax,bvh
    mov rh,ax
    call rect
    cmp bc2,0ffh
    L_JE bv_done
    mov al,bc2                         ; inner top and left
    mov ink,al
    mov ax,bvx
    inc ax
    mov rx,ax
    mov ax,bvy
    inc ax
    mov ry,ax
    mov ax,bvw
    sub ax,2
    mov rw,ax
    mov rh,1
    call rect
    mov rw,1
    mov ax,bvh
    sub ax,2
    mov rh,ax
    call rect
    mov al,bc4                         ; inner bottom and right
    mov ink,al
    mov ax,bvy
    add ax,bvh
    sub ax,2
    mov ry,ax
    mov ax,bvw
    sub ax,2
    mov rw,ax
    mov rh,1
    call rect
    mov ax,bvx
    add ax,bvw
    sub ax,2
    mov rx,ax
    mov ax,bvy
    inc ax
    mov ry,ax
    mov rw,1
    mov ax,bvh
    sub ax,2
    mov rh,ax
    call rect
bv_done:
    pop ax
    ret
bevel ENDP

; ---- horizontal two-step gradient bar gx/gy/gw/gh, colours gc1..gc3 ----
; Five equal segments: c1, c1/c2 dither, c2, c2/c3 dither, c3.
grad_solid PROC NEAR
    mov ax,gseg
    mov gwid,ax
grad_solid_w:
    mov ax,gxx
    mov rx,ax
    mov ax,gy
    mov ry,ax
    mov ax,gwid
    mov rw,ax
    mov ax,gh
    mov rh,ax
    mov al,gca
    mov ink,al
    call rect
    mov ax,gwid
    add gxx,ax
    ret
grad_solid ENDP

grad_dither PROC NEAR
    mov ax,gseg
    mov gwid,ax
    mov ax,gxx
    mov rx,ax
    mov ax,gy
    mov ry,ax
    mov ax,gwid
    mov rw,ax
    mov ax,gh
    mov rh,ax
    mov al,gca
    mov ink,al
    call rect
    mov al,gcb
    mov ink,al
    mov rw,1
    mov ax,gxx
    inc ax
    mov rx,ax
    mov bx,gxx
    add bx,gwid
gd_loop:
    cmp rx,bx
    jae gd_done
    call rect
    add rx,2
    jmp gd_loop
gd_done:
    mov ax,gwid
    add gxx,ax
    ret
grad_dither ENDP

gradbar PROC NEAR
    push ax
    push bx
    push dx
    mov ax,gw
    xor dx,dx
    mov bx,5
    div bx
    mov gseg,ax
    mov ax,gx
    mov gxx,ax
    mov al,gc1
    mov gca,al
    call grad_solid
    mov al,gc1
    mov gca,al
    mov al,gc2
    mov gcb,al
    call grad_dither
    mov al,gc2
    mov gca,al
    call grad_solid
    mov al,gc2
    mov gca,al
    mov al,gc3
    mov gcb,al
    call grad_dither
    mov al,gc3
    mov gca,al
    mov ax,gx                          ; last segment takes the remainder
    add ax,gw
    sub ax,gxx
    mov gwid,ax
    call grad_solid_w
    pop dx
    pop bx
    pop ax
    ret
gradbar ENDP

; ---- window frame with gradient title bar ----
; wox/woy origin, cw_w/cw_h size, SI title, tbact 1 = active,
; tbmin 1 = show the minimize button. Close button is always drawn.
chrome PROC NEAR
    push ax
    push bx
    push si
    mov ax,wox
    mov bvx,ax
    mov ax,woy
    mov bvy,ax
    mov ax,cw_w
    mov bvw,ax
    mov ax,cw_h
    mov bvh,ax
    mov bvm,BV_WINDOW
    call bevel
    mov ax,wox
    add ax,3
    mov gx,ax
    mov ax,woy
    add ax,3
    mov gy,ax
    mov ax,cw_w
    sub ax,6
    mov gw,ax
    mov gh,TBAR_H
    cmp tbact,0
    je ch_idle
    mov gc1,K_NAVY
    mov gc2,K_MID
    mov gc3,K_SKY
    mov tbtxt,K_WHITE
    jmp ch_bar
ch_idle:
    mov gc1,K_SHADOW                   ; dark for most of the bar so the
    mov gc2,K_SHADOW                   ; light title text stays readable
    mov gc3,K_INACT
    mov tbtxt,K_FACE
ch_bar:
    call gradbar
    mov ax,wox                         ; bold title
    add ax,7
    mov tx,ax
    mov ax,woy
    add ax,4
    mov ty,ax
    mov al,tbtxt
    mov fg,al
    call textout
    inc tx
    call textout
    mov ax,wox                         ; close button
    add ax,cw_w
    sub ax,21
    mov bvx,ax
    mov ax,woy
    add ax,5
    mov bvy,ax
    mov bvw,16
    mov bvh,14
    mov bvm,BV_BUTTON
    call bevel
    mov ax,bvx
    add ax,4
    mov xgx,ax
    mov ax,bvy
    add ax,4
    mov xgy,ax
    mov rw,2
    mov rh,1
    mov ink,K_BLACK
    xor bx,bx
ch_x:
    mov ax,xgx
    add ax,bx
    mov rx,ax
    mov ax,xgy
    add ax,bx
    mov ry,ax
    call rect
    mov ax,xgx
    add ax,6
    sub ax,bx
    mov rx,ax
    call rect
    inc bx
    cmp bx,7
    jb ch_x
    cmp tbmin,0
    je ch_done
    mov ax,wox                         ; minimize button
    add ax,cw_w
    sub ax,39
    mov bvx,ax
    mov ax,woy
    add ax,5
    mov bvy,ax
    mov bvw,16
    mov bvh,14
    mov bvm,BV_BUTTON
    call bevel
    mov ax,bvx
    add ax,4
    mov rx,ax
    mov ax,bvy
    add ax,9
    mov ry,ax
    mov rw,7
    mov rh,2
    mov ink,K_BLACK
    call rect
ch_done:
    pop si
    pop bx
    pop ax
    ret
chrome ENDP

; ---- icons: icx/icy = top-left, AL = kind ----
; 1 folder, 2 document, 3 computer, 4 power, 5 terminal, 6 store (16x16);
; 11 folder, 12 document, 13 computer, 14 terminal, 15 store (32x32).
draw_icon PROC NEAR
    push ax
    cmp al,1
    L_JNE di_k2
    IBOX 0,1,6,3,K_BLACK
    IBOX 1,2,4,2,K_YELLOW
    IBOX 0,3,16,12,K_BLACK
    IBOX 1,4,14,10,K_YELLOW
    IBOX 1,4,14,1,K_WHITE
    jmp di_done
di_k2:
    cmp al,2
    L_JNE di_k3
    IBOX 2,0,12,16,K_BLACK
    IBOX 3,1,10,14,K_WHITE
    IBOX 5,4,6,1,K_SHADOW
    IBOX 5,7,6,1,K_SHADOW
    IBOX 5,10,4,1,K_SHADOW
    jmp di_done
di_k3:
    cmp al,3
    L_JNE di_k4
    IBOX 0,1,16,11,K_BLACK
    IBOX 1,2,14,9,K_FACE
    IBOX 2,3,12,7,K_TEAL
    IBOX 5,12,6,2,K_SHADOW
    IBOX 3,14,10,2,K_SHADOW
    jmp di_done
di_k4:
    cmp al,4
    L_JNE di_k5
    IBOX 1,1,14,14,K_BLACK
    IBOX 2,2,12,12,K_RED
    IBOX 7,3,2,6,K_WHITE
    jmp di_done
di_k5:
    cmp al,5
    L_JNE di_k11
    IBOX 0,1,16,14,K_SHADOW
    IBOX 1,2,14,12,K_BLACK
    IBOX 3,5,2,2,K_FACE
    IBOX 5,7,2,2,K_FACE
    IBOX 3,9,2,2,K_FACE
    IBOX 8,10,4,2,K_FACE
    jmp di_done
di_k11:
    cmp al,11
    L_JNE di_k12
    IBOX 0,3,13,7,K_BLACK
    IBOX 1,4,11,5,K_YELLOW
    IBOX 0,8,32,22,K_BLACK
    IBOX 1,9,30,20,K_YELLOW
    IBOX 1,9,30,2,K_WHITE
    IBOX 28,10,3,19,K_SHADOW
    jmp di_done
di_k12:
    cmp al,12
    L_JNE di_k13
    IBOX 5,0,22,30,K_BLACK
    IBOX 6,1,20,28,K_WHITE
    IBOX 10,7,12,2,K_SHADOW
    IBOX 10,12,12,2,K_SHADOW
    IBOX 10,17,12,2,K_SHADOW
    IBOX 10,22,8,2,K_SHADOW
    jmp di_done
di_k13:
    cmp al,14
    L_JE di_k14
    cmp al,15
    L_JE di_k15
    cmp al,6
    L_JE di_k6
    IBOX 1,1,30,22,K_BLACK
    IBOX 2,2,28,20,K_FACE
    IBOX 4,4,24,14,K_SHADOW
    IBOX 5,5,22,12,K_TEAL
    IBOX 12,23,8,3,K_FACE
    IBOX 8,26,16,3,K_FACE
    IBOX 8,28,16,1,K_SHADOW
    IBOX 12,23,8,1,K_SHADOW
    jmp di_done
di_k14:
    IBOX 1,2,30,26,K_SHADOW
    IBOX 2,3,28,24,K_BLACK
    IBOX 2,3,28,4,K_NAVY
    IBOX 6,10,3,3,K_FACE
    IBOX 9,13,3,3,K_FACE
    IBOX 6,16,3,3,K_FACE
    IBOX 14,19,8,3,K_FACE
    jmp di_done
di_k15:                                ; a bag with a "download" arrow
    IBOX 11,2,10,2,K_BLACK
    IBOX 11,2,2,8,K_BLACK
    IBOX 19,2,2,8,K_BLACK
    IBOX 4,9,24,21,K_BLACK
    IBOX 5,10,22,19,K_ORANGE
    IBOX 5,10,22,2,K_YELLOW
    IBOX 14,14,4,7,K_WHITE
    IBOX 10,21,12,2,K_WHITE
    IBOX 12,23,8,2,K_WHITE
    IBOX 14,25,4,2,K_WHITE
    jmp di_done
di_k6:
    IBOX 5,1,6,1,K_BLACK
    IBOX 5,1,1,4,K_BLACK
    IBOX 10,1,1,4,K_BLACK
    IBOX 2,4,12,11,K_BLACK
    IBOX 3,5,10,9,K_ORANGE
    IBOX 7,6,2,4,K_WHITE
    IBOX 5,10,6,1,K_WHITE
    IBOX 6,11,4,1,K_WHITE
    IBOX 7,12,2,1,K_WHITE
di_done:
    pop ax
    ret
draw_icon ENDP

; ---- radio button circle at bbx,bby (12x12) ----
; Octagon-ish disc of size ox_n at (rx0,ry0) in colour ink.
octagon PROC NEAR
    push ax
    mov ax,icx
    add ax,2
    mov rx,ax
    mov ax,icy
    mov ry,ax
    mov ax,bvw
    sub ax,4
    mov rw,ax
    mov ax,bvw
    mov rh,ax
    call rect
    mov ax,icx
    mov rx,ax
    mov ax,icy
    add ax,2
    mov ry,ax
    mov ax,bvw
    mov rw,ax
    sub ax,4
    mov rh,ax
    call rect
    mov ax,icx
    inc ax
    mov rx,ax
    mov ax,icy
    inc ax
    mov ry,ax
    mov ax,bvw
    sub ax,2
    mov rw,ax
    mov rh,ax
    call rect
    pop ax
    ret
octagon ENDP

; ---- one button, geometry in bbx/bby/bbw/bbh, style bst, action bact ----
draw_button PROC NEAR
    mov si,blbl
    cmp bst,S_RADIO
    L_JE db_radio
    mov ax,bbx
    mov bvx,ax
    mov ax,bby
    mov bvy,ax
    mov ax,bbw
    mov bvw,ax
    mov ax,bbh
    mov bvh,ax
    cmp bst,S_DEFAULT
    jne db_plain
    mov ax,bbx                         ; black outline around the default
    mov rx,ax
    mov ax,bby
    mov ry,ax
    mov ax,bbw
    mov rw,ax
    mov ax,bbh
    mov rh,ax
    mov ink,K_BLACK
    call rect
    inc bvx
    inc bvy
    sub bvw,2
    sub bvh,2
db_plain:
    mov bvm,BV_BUTTON
    call bevel
    mov ax,bbw
    shr ax,1
    add ax,bbx
    mov ctrx,ax
    mov ax,bbh
    sub ax,16
    shr ax,1
    add ax,bby
    mov ty,ax
    mov fg,K_BLACK
    call textc
    ret
db_radio:
    mov ax,bbx
    mov icx,ax
    mov ax,bby
    add ax,6
    mov icy,ax
    mov bvw,12
    mov ink,K_SHADOW
    call octagon
    inc icx
    inc icy
    mov bvw,10
    mov ink,K_WHITE
    call octagon
    mov al,bact
    sub al,A_SEL0
    cmp al,shutsel
    jne db_radio_lbl
    mov ax,bbx
    add ax,4
    mov rx,ax
    mov ax,bby
    add ax,10
    mov ry,ax
    mov rw,4
    mov rh,4
    mov ink,K_BLACK
    call rect
db_radio_lbl:
    mov ax,bbx
    add ax,22
    mov tx,ax
    mov ax,bby
    add ax,4
    mov ty,ax
    mov fg,K_BLACK
    call textout
    ret
draw_button ENDP

; SI = table, orgx/orgy = origin added to every entry.
draw_buttons PROC NEAR
    push ax
    push si
dbs_next:
    mov ax,[si+B_X]
    cmp ax,0ffffh
    je dbs_end
    add ax,orgx
    mov bbx,ax
    mov ax,[si+B_Y]
    add ax,orgy
    mov bby,ax
    mov ax,[si+B_W]
    mov bbw,ax
    mov ax,[si+B_H]
    mov bbh,ax
    mov al,[si+B_STYLE]
    mov bst,al
    mov al,[si+B_ACT]
    mov bact,al
    mov ax,[si+B_LBL]
    mov blbl,ax
    push si
    call draw_button
    pop si
    add si,B_SIZE
    jmp dbs_next
dbs_end:
    pop si
    pop ax
    ret
draw_buttons ENDP

; SI = table, origin orgx/orgy. Returns AL = action under the pointer or 0.
hit_buttons PROC NEAR
    push bx
    push si
hb_next:
    mov ax,[si+B_X]
    cmp ax,0ffffh
    je hb_none
    add ax,orgx
    mov bx,mx
    cmp bx,ax
    jb hb_skip
    add ax,[si+B_W]
    cmp bx,ax
    jae hb_skip
    mov ax,[si+B_Y]
    add ax,orgy
    mov bx,my
    cmp bx,ax
    jb hb_skip
    add ax,[si+B_H]
    cmp bx,ax
    jae hb_skip
    mov al,[si+B_ACT]
    jmp hb_out
hb_skip:
    add si,B_SIZE
    jmp hb_next
hb_none:
    xor al,al
hb_out:
    pop si
    pop bx
    ret
hit_buttons ENDP

; AX,BX = x,y; CX,DX = w,h. CF set when the pointer is inside.
pt_in PROC NEAR
    push si
    mov si,mx
    sub si,ax
    jb pi_no
    cmp si,cx
    jae pi_no
    mov si,my
    sub si,bx
    jb pi_no
    cmp si,dx
    jae pi_no
    pop si
    stc
    ret
pi_no:
    pop si
    clc
    ret
pt_in ENDP

; ---- text formatting into DI (no terminator is written) ----

; Append the string at SI.
putstr PROC NEAR
    push ax
ps_next:
    lodsb
    or al,al
    jz ps_done
    mov [di],al
    inc di
    jmp ps_next
ps_done:
    pop ax
    ret
putstr ENDP

; Append AX in decimal.
putnum PROC NEAR
    push ax
    push bx
    push cx
    push dx
    mov bx,10
    xor cx,cx
pn_div:
    xor dx,dx
    div bx
    push dx
    inc cx
    or ax,ax
    jnz pn_div
pn_out:
    pop ax
    add al,'0'
    mov [di],al
    inc di
    loop pn_out
    pop dx
    pop cx
    pop bx
    pop ax
    ret
putnum ENDP
