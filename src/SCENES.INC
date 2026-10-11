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
    call icon_place
    mov icy,ax
    mov ax,dcolx
    add ax,28
    mov icx,ax
    mov ax,pi
    cmp ax,DESK_SYS
    jae pd_short
    add ax,11                          ; 11..15: folder, document, computer,
    call draw_icon                     ; terminal, store
    mov bx,pi
    shl bx,1
    mov si,iconlbl[bx]
    jmp pd_label
pd_short:
    mov al,16                          ; a program: its name without ".WKX"
    call draw_icon
    mov bx,pi
    sub bx,DESK_SYS
    shl bx,1
    mov si,deskptr[bx]
    mov di,OFFSET linebuf
pd_name:
    lodsb
    cmp al,'.'
    je pd_named
    or al,al
    jz pd_named
    mov [di],al
    inc di
    jmp pd_name
pd_named:
    mov byte ptr [di],0
    mov si,OFFSET linebuf
pd_label:
    call strlen
    mov ax,cx
    shl ax,1
    shl ax,1
    shl ax,1                           ; label width in pixels
    mov bx,dcolx
    add bx,44
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
    mov ax,dcolx
    add ax,44
    mov ctrx,ax
    mov ax,icy
    add ax,38
    mov ty,ax
    call textc
    inc pi
    mov ax,deskcount
    add ax,DESK_SYS
    cmp pi,ax
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
    mov ax,clockx                      ; the tray clock, at the right edge
    mov bvx,ax
    mov bvy,455
    mov bvw,90
    mov bvh,22
    mov bvm,BV_PANEL
    call bevel
    mov ax,clockx
    add ax,12
    mov tx,ax
    mov ty,458
    mov fg,K_BLACK
    mov si,OFFSET clockbuf
    call textout
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
    call menu_item
    inc bx
    cmp bx,7
    jb pm_item
    BOX 34,413,140,1,K_SHADOW
    BOX 34,414,140,1,K_WHITE
    cmp subopen,0
    je pm_end
    call paint_sub
pm_end:
    ret
paint_menu ENDP

; One Start menu item, BX = 0..6: its background, icon and text. It covers
; its whole row, so the pointer moving over the menu redraws two rows and
; nothing else. Keeps BX.
menu_item PROC NEAR
    push bx
    mov ax,bx
    mov cx,28
    mul cx
    add ax,244                         ; y of the item
    cmp bx,6
    jb mit_y
    add ax,9                           ; room for the separator
mit_y:
    mov bby,ax
    mov rx,33
    mov ry,ax
    mov rw,141
    mov rh,28
    mov ink,K_FACE
    mov fg,K_BLACK
    cmp bl,menuhot
    jne mit_bg
    mov ink,K_NAVY
    mov fg,K_WHITE
mit_bg:
    call rect
    mov icx,40
    mov ax,bby
    add ax,6
    mov icy,ax
    mov ax,bx
    inc ax                             ; 1..3 files/notes/system
    cmp ax,4
    jb mit_kind
    je mit_term
    cmp ax,5
    je mit_store
    cmp ax,6
    je mit_progs
    mov ax,4                           ; power icon for Shut Down
    jmp mit_kind
mit_progs:
    mov ax,1                           ; a folder
    jmp mit_kind
mit_store:
    mov ax,6
    jmp mit_kind
mit_term:
    mov ax,5                           ; terminal
mit_kind:
    call draw_icon
    mov tx,64
    mov ax,bby
    add ax,6
    mov ty,ax
    push bx
    shl bx,1
    mov si,menuptr[bx]
    call textout
    pop bx
    cmp bx,5                           ; Programs leads to a list
    jne mit_done
    mov tx,160
    mov si,OFFSET s_arrow
    call textout
mit_done:
    pop bx
    ret
menu_item ENDP

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
    call sub_line
    inc bx
    cmp bx,progcount
    jb psb_row
    ret
paint_sub ENDP

; One row of the Programs list, BX = its index: background, icon and name.
; Keeps BX.
sub_line PROC NEAR
    push bx
    call sub_top
    add ax,3
    mov bby,ax
    mov ax,bx
    mov cx,SUB_ROW
    mul cx
    add bby,ax
    mov rx,SUB_X+3
    mov ax,bby
    mov ry,ax
    mov rw,SUB_W-6
    mov rh,SUB_ROW
    mov ink,K_FACE
    mov fg,K_BLACK
    cmp bl,subhot
    jne sbr_bg
    mov ink,K_NAVY
    mov fg,K_WHITE
sbr_bg:
    call rect
    mov icx,SUB_X+8
    mov ax,bby
    inc ax
    mov icy,ax
    mov al,7
    call draw_icon
    shl bx,1
    mov si,progptr[bx]                 ; the name without ".WKX"
    mov di,OFFSET linebuf
sbr_name:
    lodsb
    cmp al,'.'
    je sbr_end
    or al,al
    jz sbr_end
    mov [di],al
    inc di
    jmp sbr_name
sbr_end:
    mov byte ptr [di],0
    mov tx,SUB_X+30
    mov ax,bby
    inc ax
    mov ty,ax
    mov si,OFFSET linebuf
    call textout
    pop bx
    ret
sub_line ENDP

; ---- welcome dialog ----
paint_welcome PROC NEAR
    mov ax,dlgx
    add ax,WEL_X
    mov wox,ax
    mov woy,WEL_Y
    mov cw_w,WEL_W
    mov cw_h,WEL_H
    mov tbact,1
    mov tbmin,0
    mov si,OFFSET ttl_welcome
    call chrome
    mov ax,wox
    add ax,144-WEL_X
    mov icx,ax
    mov icy,136
    mov al,13
    call draw_icon
    mov ax,wox
    add ax,196-WEL_X
    mov tx,ax
    mov ty,132
    mov fg,K_NAVY
    mov bsc,2
    mov si,OFFSET w_title
    call bigtext
    WTXT 196-WEL_X,184-WEL_Y,K_BLACK,w_sub
    WTXT 196-WEL_X,208-WEL_Y,K_BLACK,w_body
    WTXT 196-WEL_X,240-WEL_Y,K_SHADOW,w_hint
    WBOX 136-WEL_X,284-WEL_Y,368,1,K_SHADOW
    WBOX 136-WEL_X,285-WEL_Y,368,1,K_WHITE
    mov ax,dlgx
    mov orgx,ax
    mov orgy,0
    mov si,OFFSET welcome_tab
    call draw_buttons
    ret
paint_welcome ENDP

; ---- Shut Down dialog ----
paint_shutdown PROC NEAR
    mov ax,dlgx
    add ax,SHD_X
    mov wox,ax
    mov woy,SHD_Y
    mov cw_w,SHD_W
    mov cw_h,SHD_H
    mov tbact,1
    mov tbmin,0
    mov si,OFFSET ttl_shut
    call chrome
    mov ax,wox
    add ax,160-SHD_X
    mov icx,ax
    mov icy,146
    mov al,13
    call draw_icon
    WTXT 204-SHD_X,148-SHD_Y,K_BLACK,sd_text
    WTXT 156-SHD_X,290-SHD_Y,K_SHADOW,sd_hint
    mov ax,dlgx
    mov orgx,ax
    mov orgy,0
    mov si,OFFSET shut_tab
    call draw_buttons
    ret
paint_shutdown ENDP

; ---- "safe to power off" ----
paint_offscreen PROC NEAR
    BOX 0,0,SCR_W,SCR_H,K_BLACK
    mov fg,K_ORANGE
    mov bsc,2
    mov ax,dlgx
    add ax,192
    mov tx,ax
    mov ty,150
    mov si,OFFSET of_1
    call bigtext
    mov ax,dlgx
    add ax,208
    mov tx,ax
    mov ty,190
    mov si,OFFSET of_2
    call bigtext
    mov ax,dlgx
    add ax,232
    mov tx,ax
    mov ty,230
    mov si,OFFSET of_3
    call bigtext
    mov ax,dlgx
    add ax,252
    mov tx,ax
    mov ty,330
    mov fg,K_SHADOW
    mov si,OFFSET of_hint
    call textout
    ret
paint_offscreen ENDP
