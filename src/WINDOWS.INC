; WINDOWS.INC - window drawing, window bodies, hit testing and dragging.

; BX = window index 0..2; only opened, un-minimized windows are drawn.
draw_if_open PROC NEAR
    mov cx,bx
    mov al,1
    shl al,cl
    test opened,al
    L_JZ dio_no
    test hidden,al
    L_JNZ dio_no
    push bx
    call draw_window
    pop bx
dio_no:
    ret
draw_if_open ENDP

draw_window PROC NEAR
    shl bx,1
    mov curwin,bx
    mov ax,wx[bx]
    mov wox,ax
    mov ax,wy[bx]
    mov woy,ax
    mov cw_w,WIN_W
    mov cw_h,WIN_H
    mov tbmin,1
    mov tbact,0
    mov ax,bx
    shr ax,1
    inc ax
    cmp al,active
    jne dw_idle
    mov tbact,1
dw_idle:
    mov si,titleptr[bx]
    call chrome
    mov bx,curwin
    cmp bx,0
    jne dw_notfiles
    call body_files
    ret
dw_notfiles:
    cmp bx,2
    jne dw_notnotes
    call body_notes
    ret
dw_notnotes:
    cmp bx,6
    jne dw_system
    call body_term
    ret
dw_system:
    call body_system
    ret
draw_window ENDP

; Right-aligned text inside the right-hand status panel: SI = string.
footer_right PROC NEAR
    call strlen
    shl cx,1
    shl cx,1
    shl cx,1
    mov ax,WIN_W-12
    sub ax,cx
    add ax,wox
    mov tx,ax
    mov ax,woy
    add ax,WIN_H-24
    mov ty,ax
    mov fg,K_BLACK
    call textout
    ret
footer_right ENDP

; Etched line under the toolbar, status panels along the bottom.
window_bands PROC NEAR
    WBOX 3,52,WIN_W-6,1,K_SHADOW
    WBOX 3,53,WIN_W-6,1,K_WHITE
    WBEV 6,WIN_H-26,200,20,BV_PANEL
    WBEV 208,WIN_H-26,WIN_W-214,20,BV_PANEL
    ret
window_bands ENDP

; ---------------------------------------------------------------- Files
body_files PROC NEAR
    call window_bands
    mov ax,wox
    mov orgx,ax
    mov ax,woy
    mov orgy,ax
    mov si,OFFSET files_tab
    call draw_buttons
    WBEV 6,58,WIN_W-12,190,BV_FIELD
    mov di,OFFSET linebuf              ; "Page n/m"
    mov si,OFFSET s_page
    call putstr
    mov ax,filepage
    xor dx,dx
    mov bx,20
    div bx
    inc ax
    call putnum
    mov byte ptr [di],'/'
    inc di
    mov ax,filecount
    add ax,19
    xor dx,dx
    div bx
    or ax,ax
    jnz bf_pages
    mov ax,1
bf_pages:
    call putnum
    mov byte ptr [di],0
    WTXT WIN_W-96,29,K_BLACK,linebuf
    mov di,OFFSET linebuf              ; status: "n object(s)"
    mov ax,filecount
    call putnum
    mov si,OFFSET s_objects
    call putstr
    mov byte ptr [di],0
    WTXT 12,WIN_H-24,K_BLACK,linebuf
    WTXT 214,WIN_H-24,K_BLACK,s_root
    mov ax,filecount
    sub ax,filepage
    cmp ax,20
    jbe bf_count
    mov ax,20
bf_count:
    mov fcnt,ax
    or ax,ax
    jnz bf_rows
    WTXT 16,66,K_SHADOW,s_nofiles
    ret
bf_rows:
    mov fk,0
bf_loop:
    mov ax,fk                          ; column = k / 10, row = k % 10
    xor dx,dx
    mov bx,10
    div bx
    mov frow,dx
    mov bx,200
    mul bx
    add ax,wox
    add ax,12
    mov fxx,ax
    mov ax,frow
    mov bx,18
    mul bx
    add ax,woy
    add ax,64
    mov fyy,ax
    mov ax,filepage
    add ax,fk
    mov bx,14
    mul bx
    add ax,OFFSET files
    mov si,ax
    mov al,[si]                        ; attribute: 10h = directory
    inc si
    mov cl,2
    test al,10h
    jz bf_isfile
    mov cl,1
bf_isfile:
    mov ax,fxx
    mov icx,ax
    mov ax,fyy
    mov icy,ax
    mov al,cl
    call draw_icon
    mov ax,fxx
    add ax,22
    mov tx,ax
    mov ax,fyy
    mov ty,ax
    mov fg,K_BLACK
    call textout
    inc fk
    mov ax,fk
    cmp ax,fcnt
    L_JB bf_loop
    ret
body_files ENDP

; ---------------------------------------------------------------- Notes
body_notes PROC NEAR
    call window_bands
    mov ax,wox
    mov orgx,ax
    mov ax,woy
    mov orgy,ax
    mov si,OFFSET notes_tab
    call draw_buttons
    WTXT 108,29,K_SHADOW,n_sub
    WBEV 6,58,WIN_W-12,190,BV_FIELD
    mov si,status
    WTXTR 12,WIN_H-24,K_BLACK
    mov di,OFFSET linebuf              ; "n/240" in the right panel
    mov ax,notelen
    call putnum
    mov si,OFFSET s_limit
    call putstr
    mov byte ptr [di],0
    mov si,OFFSET linebuf
    call footer_right
    mov si,OFFSET note
    mov cx,notelen
    mov nline,0
bn_line:
    mov di,OFFSET linebuf
    xor bx,bx                          ; characters collected for this row
bn_char:
    cmp cx,0
    je bn_last
    mov al,[si]
    cmp al,13
    je bn_cr
    cmp al,10
    je bn_lf
    mov [di],al
    inc di
    inc si
    dec cx
    inc bx
    cmp bx,48
    jb bn_char
    jmp bn_flush
bn_cr:
    inc si
    dec cx
    jmp bn_flush
bn_lf:
    inc si
    dec cx
    jmp bn_char
bn_flush:                              ; row ended before the text did
    mov byte ptr [di],0
    push si
    push cx
    call note_row
    pop cx
    pop si
    inc nline
    cmp nline,10
    jb bn_line
    ret
bn_last:                               ; final row, with the caret after it
    cmp nline,10
    jae bn_end
    mov byte ptr [di],0
    push bx
    call note_row
    pop bx
    cmp active,2
    jne bn_end
    shl bx,1
    shl bx,1
    shl bx,1
    mov ax,wox
    add ax,12
    add ax,bx
    mov rx,ax
    call note_row_y
    mov ry,ax
    mov rw,2
    mov rh,16
    mov ink,K_BLACK
    call rect
bn_end:
    ret
body_notes ENDP

; AX = pixel row of note line nline.
note_row_y PROC NEAR
    push bx
    mov ax,nline
    mov bx,18
    mul bx
    add ax,woy
    add ax,64
    pop bx
    ret
note_row_y ENDP

; Draw linebuf as note line nline.
note_row PROC NEAR
    push ax
    push si
    mov ax,wox
    add ax,12
    mov tx,ax
    call note_row_y
    mov ty,ax
    mov fg,K_BLACK
    mov si,OFFSET linebuf
    call textout
    pop si
    pop ax
    ret
note_row ENDP

; --------------------------------------------------------------- System
body_system PROC NEAR
    WBEV 6,45,WIN_W-12,WIN_H-51,BV_BUTTON      ; tab page
    WBEV 8,24,76,24,BV_BUTTON                  ; "General" tab
    WBTXT 22,29,K_BLACK,sy_tab
    mov ax,wox
    add ax,22
    mov icx,ax
    mov ax,woy
    add ax,66
    mov icy,ax
    mov al,13
    call draw_icon
    WBTXT 70,64,K_BLACK,sy_h1
    WTXT 90,84,K_BLACK,sy_a1
    WTXT 90,104,K_BLACK,sy_a2
    WBTXT 70,136,K_BLACK,sy_h2
    WTXT 90,156,K_BLACK,sy_b1
    WTXT 90,176,K_BLACK,sy_b2
    WTXT 90,196,K_BLACK,sy_b3
    WBOX 20,228,WIN_W-40,1,K_SHADOW
    WBOX 20,229,WIN_W-40,1,K_WHITE
    WTXT 22,238,K_BLACK,sy_f1
    WTXT 22,256,K_BLACK,sy_f2
    ret
body_system ENDP

; ------------------------------------------------- hit testing, dragging

; Make window number AL (1..3) the visible active window.
open_window PROC NEAR
    mov active,al
    mov cl,al
    dec cl
    mov al,1
    shl al,cl
    or opened,al
    not al
    and hidden,al
    mov menuopen,0
    mov dirty,1
    ret
open_window ENDP

close_window PROC NEAR
    mov cl,active
    dec cl
    mov al,1
    shl al,cl
    not al
    and opened,al
    and hidden,al
    mov dirty,1
    ret
close_window ENDP

; Hide the active window and hand focus to another visible one.
minimize_window PROC NEAR
    mov cl,active
    dec cl
    mov al,1
    shl al,cl
    or hidden,al
    mov bx,3
mz_next:
    mov cx,bx
    mov al,1
    shl al,cl
    test opened,al
    jz mz_skip
    test hidden,al
    jnz mz_skip
    inc bl
    mov active,bl
    jmp mz_done
mz_skip:
    dec bx
    jns mz_next
mz_done:
    mov dirty,1
    ret
minimize_window ENDP

; BX = window index 0..2. CF set when the click was consumed.
hit_window PROC NEAR
    push ax
    push bx
    push cx
    push dx
    push si
    mov cx,bx
    mov al,1
    shl al,cl
    test opened,al
    L_JZ hw_no
    test hidden,al
    L_JNZ hw_no
    shl bx,1
    mov ax,mx
    sub ax,wx[bx]
    L_JB hw_no
    cmp ax,WIN_W
    L_JAE hw_no
    mov dx,my
    sub dx,wy[bx]
    L_JB hw_no
    cmp dx,WIN_H
    L_JAE hw_no
    mov cx,bx
    shr cx,1
    inc cx
    mov active,cl                      ; raise
    mov menuopen,0
    cmp dx,21
    jae hw_body
    cmp dx,5                           ; title buttons live at y 5..18
    jb hw_drag
    cmp dx,19
    jae hw_drag
    cmp ax,WIN_W-21
    jb hw_notclose
    cmp ax,WIN_W-5
    jae hw_drag
    call close_window
    jmp hw_yes
hw_notclose:
    cmp ax,WIN_W-39
    jb hw_drag
    cmp ax,WIN_W-23
    jae hw_drag
    call minimize_window
    jmp hw_yes
hw_drag:
    mov drag,1
    mov dragdx,ax
    mov dragdy,dx
    jmp hw_yes
hw_body:
    mov ax,wx[bx]
    mov orgx,ax
    mov ax,wy[bx]
    mov orgy,ax
    cmp bx,0
    jne hw_notes
    mov si,OFFSET files_tab
    jmp hw_tab
hw_notes:
    cmp bx,2
    jne hw_yes
    mov si,OFFSET notes_tab
hw_tab:
    call hit_buttons
    or al,al
    jnz hw_act
    cmp bx,0                           ; no button: a file in the list?
    jne hw_yes
    call files_click
    jmp hw_yes
hw_act:
    call do_action
hw_yes:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret
hw_no:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
hit_window ENDP

; Follow the pointer while dragging; the repaint is a blit, not a redraw.
move_window PROC NEAR
    xor bx,bx
    mov bl,active
    dec bx
    shl bx,1
    cmp winmoved,0
    jne mw_have
    mov ax,wx[bx]
    mov oldx,ax
    mov ax,wy[bx]
    mov oldy,ax
mw_have:
    mov ax,mx
    sub ax,dragdx
    jns mw_x1
    xor ax,ax
mw_x1:
    cmp ax,WIN_XMAX
    jbe mw_x2
    mov ax,WIN_XMAX
mw_x2:
    and ax,0fff8h
    cmp wx[bx],ax
    je mw_y
    mov wx[bx],ax
    mov winmoved,1
mw_y:
    mov ax,my
    sub ax,dragdy
    cmp ax,WIN_YMIN
    jge mw_y1
    mov ax,WIN_YMIN
mw_y1:
    cmp ax,WIN_YMAX
    jle mw_y2
    mov ax,WIN_YMAX
mw_y2:
    cmp wy[bx],ax
    je mw_done
    mov wy[bx],ax
    mov winmoved,1
mw_done:
    ret
move_window ENDP
