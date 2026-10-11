; PGUI.INC - a window for native programs: INT 80h functions 10h..14h.
;
; A program gets one window, WIN_W x WIN_H at GUI_X/GUI_Y, on top of the
; desktop. It draws with client coordinates (0,0 is the pixel under the
; left end of the title bar; the client area is 410 x 264) and everything is
; clipped to the window. The pointer is only on screen while the program
; waits for an event. The window goes away when the program exits.
; These handlers are entered from sys_call (TERM.INC) with BP on the saved
; registers and DS = ES = the kernel.

; Copy the caller's string at ES:SI to DI: at most CX characters, then NUL.
gui_copy PROC NEAR
gcp_next:
    mov al,es:[si]
    or al,al
    jz gcp_end
    mov [di],al
    inc si
    inc di
    loop gcp_next
gcp_end:
    mov byte ptr [di],0
    ret
gui_copy ENDP

gui_fullclip PROC NEAR
    mov clx0,0
    mov cly0,0
    mov clx1,SCR_W
    mov cly1,SCR_H
    ret
gui_fullclip ENDP

; The caller's BX,CX (client x,y) -> AX,DX in screen pixels.
gui_xy PROC NEAR
    mov ax,[bp+14]
    add ax,GUI_X+GUI_CX
    mov dx,[bp+12]
    add dx,GUI_Y+GUI_CY
    ret
gui_xy ENDP

; AH=10h: open (or wipe) the window. DS:DX = title.
sys_gopen:
    mov es,[bp+2]
    mov si,[bp+10]
    mov di,OFFSET guititle
    mov cx,40
    call gui_copy
    push cs
    pop es
    cmp guiopen,0
    jne go_draw
    call restore_under                 ; take the pointer off the screen
    mov guiopen,1
go_draw:
    call gui_fullclip
    mov wox,GUI_X
    mov woy,GUI_Y
    mov cw_w,WIN_W
    mov cw_h,WIN_H
    mov tbact,1
    mov tbmin,0
    mov si,OFFSET guititle
    call chrome
    mov clx0,GUI_X                     ; from now on: inside the frame only
    mov clx1,GUI_X+WIN_W
    mov cly0,GUI_Y+GUI_CY
    mov cly1,GUI_Y+WIN_H-3
    jmp sys_back

; AH=11h: rectangle. BX,CX = x,y; SI,DI = width,height; AL = colour.
sys_grect:
    cmp guiopen,0
    L_JE sys_back
    call gui_xy
    mov rx,ax
    mov ry,dx
    mov ax,[bp+8]
    mov rw,ax
    mov ax,[bp+6]
    mov rh,ax
    mov al,[bp+16]
    and al,15
    mov ink,al
    call rect
    jmp sys_back

; AH=12h: text. BX,CX = x,y; AL = colour; DS:DX = string.
sys_gtext:
    cmp guiopen,0
    L_JE sys_back
    mov es,[bp+2]
    mov si,[bp+10]
    mov di,OFFSET linebuf
    mov cx,62
    call gui_copy
    push cs
    pop es
    call gui_xy
    mov tx,ax
    mov ty,dx
    mov al,[bp+16]
    and al,15
    mov fg,al
    mov si,OFFSET linebuf
    call textout
    jmp sys_back

; AH=13h: 3D box. BX,CX = x,y; SI,DI = width,height; AL = bevel style.
sys_gbevel:
    cmp guiopen,0
    L_JE sys_back
    call gui_xy
    mov bvx,ax
    mov bvy,dx
    mov ax,[bp+8]
    mov bvw,ax
    mov ax,[bp+6]
    mov bvh,ax
    mov al,[bp+16]
    cmp al,BV_PRESS
    jbe gb_style
    xor al,al
gb_style:
    mov bvm,al
    call bevel
    jmp sys_back

; AH=14h: wait for an event. AX = 1 key (BL ASCII, BH scan code),
; 2 click (BX,CX = client x,y), 3 the close button; 0 without a window.
sys_gevent:
    mov word ptr [bp+16],0
    cmp guiopen,0
    L_JE sys_back
    call cursor_show
ge_loop:
    cmp curdirty,0
    je ge_btn
    mov curdirty,0
    call cursor_move
ge_btn:
    mov al,buttons
    cmp al,lastbuttons
    je ge_key
    mov lastbuttons,al
    test al,1
    jz ge_key
    mov ax,mx                          ; a press: where in the window?
    sub ax,GUI_X
    jb ge_key
    cmp ax,WIN_W
    jae ge_key
    mov dx,my
    sub dx,GUI_Y
    jb ge_key
    cmp dx,WIN_H
    jae ge_key
    cmp dx,GUI_CY
    jae ge_client
    cmp dx,5                           ; the close button, as in chrome
    jb ge_key
    cmp dx,19
    jae ge_key
    cmp ax,WIN_W-21
    jb ge_key
    cmp ax,WIN_W-5
    jae ge_key
    mov word ptr [bp+16],3
    jmp ge_out
ge_client:
    sub ax,GUI_CX
    sub dx,GUI_CY
    mov [bp+14],ax
    mov [bp+12],dx
    mov word ptr [bp+16],2
    jmp ge_out
ge_key:
    mov ah,1
    int 16h
    jz ge_idle
    mov ah,0
    int 16h
    mov [bp+14],ax
    mov word ptr [bp+16],1
    jmp ge_out
ge_idle:
    sti                                ; let the mouse callback run
    hlt
    jmp ge_loop
ge_out:
    call restore_under
    jmp sys_back

; Load and run the Store program (Start menu, desktop icon, F7).
run_store PROC NEAR
    mov menuopen,0
    mov iconsel,0
    call paint                         ; the program draws on a tidy desktop
    mov si,OFFSET storefile
    call run_file
    mov dirty,1
    ret
run_store ENDP
