; SCENES.INC - desktop, icons, taskbar, Start menu and the two dialogs.

paint PROC NEAR
    cmp viewstate,V_OFF
    L_JE paint_off
    call paint_desktop
    xor bx,bx                          ; windows behind the active one
paint_behind:
    mov ax,bx
    inc ax
    cmp al,active
    je paint_next
    call draw_if_open
paint_next:
    inc bx
    cmp bx,4
    jb paint_behind
    xor bx,bx                          ; the active window on top
    mov bl,active
    dec bx
    call draw_if_open
    call paint_taskbar
    cmp menuopen,0
    je paint_nomenu
    call paint_menu
paint_nomenu:
    cmp viewstate,V_START
    jne paint_notwelcome
    call paint_welcome
paint_notwelcome:
    cmp viewstate,V_SHUTDOWN
    jne paint_pointer
    call paint_shutdown
    jmp paint_pointer
paint_off:
    call paint_offscreen
paint_pointer:
    cmp clipped,0
    jne paint_end
    call cursor_show
paint_end:
    ret
paint ENDP

; ---- desktop and icons ----
paint_desktop PROC NEAR
    BOX 0,0,SCR_W,TASK_Y,K_TEAL
    mov pi,0
pd_icon:
    mov ax,pi
    mov cx,80
    mul cx
    add ax,16
    mov icy,ax
    mov icx,28
    mov ax,pi
    add ax,11                          ; 11..15: folder, document, computer,
                                       ; terminal, store
    call draw_icon
    mov bx,pi
    shl bx,1
    mov si,iconlbl[bx]
    call strlen
    mov ax,cx
    shl ax,1
    shl ax,1
    shl ax,1                           ; label width in pixels
    mov bx,44
    mov dx,ax
    shr dx,1
    sub bx,dx
    sub bx,2                           ; highlight x
    mov fg,K_WHITE
    mov ax,pi
    inc ax
    cmp al,iconsel
    jne pd_plain
    mov rx,bx
    mov ax,icy
    add ax,36
    mov ry,ax
    mov ax,cx
    shl ax,1
    shl ax,1
    shl ax,1
    add ax,4
    mov rw,ax
    mov rh,19
    mov ink,K_NAVY
    call rect
pd_plain:
    mov ctrx,44
    mov ax,icy
    add ax,38
    mov ty,ax
    call textc
    inc pi
    cmp pi,5
    L_JB pd_icon
    ret
paint_desktop ENDP

; ---- taskbar ----
paint_taskbar PROC NEAR
    BOX 0,TASK_Y,SCR_W,TASK_H,K_FACE
    BOX 0,TASK_Y,SCR_W,1,K_LIGHT
    BOX 0,TASK_Y+1,SCR_W,1,K_WHITE
    BVL 2,455,72,22,BV_BUTTON
    cmp menuopen,0
    je pt_start
    BVL 2,455,72,22,BV_PRESS
pt_start:
    BOX 8,459,6,6,K_RED
    BOX 15,459,6,6,K_GREEN
    BOX 8,466,6,6,K_BLUE
    BOX 15,466,6,6,K_YELLOW
    BTXT 28,458,K_BLACK,lbl_start
    BOX 80,457,1,18,K_SHADOW
    BOX 81,457,1,18,K_WHITE
    xor bx,bx
    mov slot,0
pt_loop:
    mov cx,bx
    mov al,1
    shl al,cl
    test opened,al
    L_JZ pt_next
    mov ax,slot                        ; x = 88 + slot * 108
    mov cx,108
    mul cx
    add ax,88
    mov bvx,ax
    mov bvy,455
    mov bvw,104
    mov bvh,22
    mov bvm,BV_BUTTON
    mov ax,bx
    inc ax
    cmp al,active
    jne pt_draw
    mov cx,bx
    mov al,1
    shl al,cl
    test hidden,al
    jnz pt_draw
    mov bvm,BV_PRESS
pt_draw:
    push bx
    call bevel
    mov ax,bvx                         ; small icon + title
    add ax,6
    mov icx,ax
    mov icy,458
    pop bx
    push bx
    mov ax,bx
    inc ax
    cmp ax,4
    jne pt_icon
    mov ax,5                           ; terminal
pt_icon:
    call draw_icon
    pop bx
    push bx
    mov ax,bvx
    add ax,28
    mov tx,ax
    mov ty,458
    mov fg,K_BLACK
    shl bx,1
    mov si,iconlbl[bx]
    call textout
    pop bx
    inc slot
pt_next:
    inc bx
    cmp bx,4
    L_JB pt_loop
    BVL 548,455,90,22,BV_PANEL
    TXT 560,458,K_BLACK,clockbuf
    ret
paint_taskbar ENDP

; ---- Start menu ----
paint_menu PROC NEAR
    BVL MENU_X,MENU_Y,MENU_W,MENU_H,BV_WINDOW
    BOX 5,244,26,205,K_SHADOW
    mov tx,9
    mov ty,444
    mov fg,K_WHITE
    mov si,OFFSET banner
    call rottext
    mov tx,10
    call rottext
    xor bx,bx                          ; items 0..6
pm_item:
    mov ax,bx
    mov cx,28
    mul cx
    add ax,244                         ; y of the item
    cmp bx,6
    jb pm_y
    add ax,9                           ; room for the separator
pm_y:
    mov bby,ax
    mov fg,K_BLACK
    cmp bl,menuhot
    jne pm_draw
    mov rx,33
    mov ry,ax
    mov rw,141
    mov rh,28
    mov ink,K_NAVY
    call rect
    mov fg,K_WHITE
pm_draw:
    push bx
    mov icx,40
    mov ax,bby
    add ax,6
    mov icy,ax
    mov ax,bx
    inc ax                             ; 1..3 files/notes/system
    cmp ax,4
    jb pm_kind
    je pm_term
    cmp ax,5
    je pm_store
    cmp ax,6
    je pm_progs
    mov ax,4                           ; power icon for Shut Down
    jmp pm_kind
pm_progs:
    mov ax,1                           ; a folder
    jmp pm_kind
pm_store:
    mov ax,6
    jmp pm_kind
pm_term:
    mov ax,5                           ; terminal
pm_kind:
    call draw_icon
    pop bx
    push bx
    mov tx,64
    mov ax,bby
    add ax,6
    mov ty,ax
    shl bx,1
    mov si,menuptr[bx]
    call textout
    pop bx
    inc bx
    cmp bx,7
    L_JB pm_item
    BOX 34,413,140,1,K_SHADOW
    BOX 34,414,140,1,K_WHITE
    mov tx,160                         ; Programs leads to a list
    mov ty,390
    mov fg,K_BLACK
    cmp menuhot,5
    jne pm_arrow
    mov fg,K_WHITE
pm_arrow:
    mov si,OFFSET s_arrow
    call textout
    cmp subopen,0
    je pm_end
    call paint_sub
pm_end:
    ret
paint_menu ENDP

; ---- the Programs list: one row per .WKX on the disk ----

; AX = rows in the list (an empty one still has a row).
sub_rows PROC NEAR
    mov ax,progcount
    or ax,ax
    jnz sr_done
    inc ax
sr_done:
    ret
sub_rows ENDP

; AX = y of the top of the list; its bottom lines up with the menu's.
sub_top PROC NEAR
    push cx
    push dx
    call sub_rows
    mov cx,SUB_ROW
    mul cx
    add ax,6
    mov cx,ax
    mov ax,449
    sub ax,cx
    pop dx
    pop cx
    ret
sub_top ENDP

paint_sub PROC NEAR
    call sub_rows
    mov cx,SUB_ROW
    mul cx
    add ax,6
    mov bvh,ax
    call sub_top
    mov bvy,ax
    mov bvx,SUB_X
    mov bvw,SUB_W
    mov bvm,BV_WINDOW
    call bevel
    cmp progcount,0
    jne psb_rows
    mov ax,bvy
    add ax,4
    mov ty,ax
    mov tx,SUB_X+12
    mov fg,K_SHADOW
    mov si,OFFSET s_noprogs
    call textout
    ret
psb_rows:
    xor bx,bx
psb_row:
    push bx
    call sub_top
    add ax,3
    mov bby,ax
    mov ax,bx
    mov cx,SUB_ROW
    mul cx
    add bby,ax
    mov fg,K_BLACK
    cmp bl,subhot
    jne psb_draw
    mov rx,SUB_X+3
    mov ax,bby
    mov ry,ax
    mov rw,SUB_W-6
    mov rh,SUB_ROW
    mov ink,K_NAVY
    call rect
    mov fg,K_WHITE
psb_draw:
    mov icx,SUB_X+8
    mov ax,bby
    inc ax
    mov icy,ax
    mov al,7
    call draw_icon
    pop bx
    push bx
    shl bx,1
    mov si,progptr[bx]                 ; the name without ".WKX"
    mov di,OFFSET linebuf
psb_name:
    lodsb
    cmp al,'.'
    je psb_end
    or al,al
    jz psb_end
    mov [di],al
    inc di
    jmp psb_name
psb_end:
    mov byte ptr [di],0
    mov tx,SUB_X+30
    mov ax,bby
    inc ax
    mov ty,ax
    mov si,OFFSET linebuf
    call textout
    pop bx
    inc bx
    cmp bx,progcount
    L_JB psb_row
    ret
paint_sub ENDP

; ---- welcome dialog ----
paint_welcome PROC NEAR
    mov wox,WEL_X
    mov woy,WEL_Y
    mov cw_w,WEL_W
    mov cw_h,WEL_H
    mov tbact,1
    mov tbmin,0
    mov si,OFFSET ttl_welcome
    call chrome
    mov icx,144
    mov icy,136
    mov al,13
    call draw_icon
    TXTBIG 196,124,K_NAVY,3,w_title
    TXT 196,184,K_BLACK,w_sub
    TXT 196,208,K_BLACK,w_body
    TXT 196,240,K_SHADOW,w_hint
    BOX 136,284,368,1,K_SHADOW
    BOX 136,285,368,1,K_WHITE
    mov orgx,0
    mov orgy,0
    mov si,OFFSET welcome_tab
    call draw_buttons
    ret
paint_welcome ENDP

; ---- Shut Down dialog ----
paint_shutdown PROC NEAR
    mov wox,SHD_X
    mov woy,SHD_Y
    mov cw_w,SHD_W
    mov cw_h,SHD_H
    mov tbact,1
    mov tbmin,0
    mov si,OFFSET ttl_shut
    call chrome
    mov icx,160
    mov icy,146
    mov al,13
    call draw_icon
    TXT 204,148,K_BLACK,sd_text
    TXT 156,290,K_SHADOW,sd_hint
    mov orgx,0
    mov orgy,0
    mov si,OFFSET shut_tab
    call draw_buttons
    ret
paint_shutdown ENDP

; ---- "safe to power off" ----
paint_offscreen PROC NEAR
    BOX 0,0,SCR_W,SCR_H,K_BLACK
    TXTBIG 192,150,K_ORANGE,2,of_1
    TXTBIG 208,190,K_ORANGE,2,of_2
    TXTBIG 232,230,K_ORANGE,2,of_3
    TXT 252,330,K_SHADOW,of_hint
    ret
paint_offscreen ENDP
