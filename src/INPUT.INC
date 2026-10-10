; INPUT.INC - PS/2 mouse callback, keyboard, click and action dispatch.

; BIOS callback stack: status, X delta, Y delta, reserved, return CS:IP.
; Motion only flags the pointer (curdirty); the main loop swaps the arrow.
mouse_callback PROC FAR
    push bp
    mov bp,sp
    push ax
    push bx
    push ds
    push cs
    pop ds
    mov ax,[bp+12]
    mov buttons,al
    mov bx,[bp+10]
    test al,10h
    L_JZ mc_xpos
    or bx,0ff00h
mc_xpos:
    add bx,mx
    L_JNS mc_xlow
    xor bx,bx
mc_xlow:
    cmp bx,SCR_W-1
    L_JLE mc_xhigh
    mov bx,SCR_W-1
mc_xhigh:
    cmp mx,bx
    L_JE mc_xsame
    mov curdirty,1
mc_xsame:
    mov mx,bx
    mov bx,[bp+8]
    test al,20h
    L_JZ mc_ypos
    or bx,0ff00h
mc_ypos:
    neg bx
    add bx,my
    L_JNS mc_ylow
    xor bx,bx
mc_ylow:
    cmp bx,SCR_H-1
    L_JLE mc_yhigh
    mov bx,SCR_H-1
mc_yhigh:
    cmp bx,my
    L_JE mc_ysame
    mov curdirty,1
mc_ysame:
    mov my,bx
    pop ds
    pop bx
    pop ax
    pop bp
    retf
mouse_callback ENDP

; Execute an action code in AL.
do_action PROC NEAR
    cmp al,A_NONE
    L_JE da_done
    mov dirty,1
    cmp al,A_TERM
    L_JA da_other
    jmp open_window                    ; 1..4 are window numbers
da_other:
    cmp al,A_SHUTDOWN
    L_JNE da_enter
    mov viewstate,V_SHUTDOWN
    mov menuopen,0
    ret
da_enter:
    cmp al,A_ENTER
    L_JNE da_restart
    mov viewstate,V_DESKTOP
    ret
da_restart:
    cmp al,A_RESTART
    L_JE reboot
    cmp al,A_POWER
    L_JE poweroff
    cmp al,A_CANCEL
    L_JNE da_files
    mov viewstate,V_DESKTOP
    ret
da_files:
    cmp al,A_REFRESH
    L_JNE da_prev
    jmp refresh_files
da_prev:
    cmp al,A_PREV
    L_JE previous_files
    cmp al,A_NEXT
    L_JE next_files
    cmp al,A_SAVE
    L_JE save_note
    cmp al,A_OK
    L_JNE da_sel
    mov al,shutsel                     ; OK in the Shut Down dialog
    cmp al,0
    L_JE poweroff
    jmp reboot
da_sel:
    cmp al,A_SEL0
    L_JB da_done
    cmp al,A_SEL1
    L_JA da_done
    sub al,A_SEL0
    mov shutsel,al
da_done:
    ret
do_action ENDP

; Start menu item under the pointer: AL = 0..3 or 0FFh.
menu_item_at PROC NEAR
    push bx
    mov ax,mx
    cmp ax,33
    jb mi_none
    cmp ax,175
    jae mi_none
    mov ax,my
    cmp ax,300
    jb mi_none
    cmp ax,449
    jae mi_none
    cmp ax,412
    jb mi_items
    cmp ax,421
    jb mi_none
    mov al,4
    jmp mi_out
mi_items:
    sub ax,300
    mov bl,28
    div bl
    jmp mi_out
mi_none:
    mov al,0ffh
mi_out:
    pop bx
    ret
menu_item_at ENDP

; A click while the Start menu is open: it always closes.
menu_click PROC NEAR
    mov menuopen,0
    mov ax,2                           ; the Start button just closes it
    mov bx,455
    mov cx,72
    mov dx,22
    call pt_in
    jc mcl_done
    call menu_item_at
    cmp al,0ffh
    je mcl_done
    cmp al,4
    je mcl_shut
    inc al                             ; Files / Notes / System / WinKit
    jmp do_action
mcl_shut:
    mov al,A_SHUTDOWN
    jmp do_action
mcl_done:
    ret
menu_click ENDP

; Click or hide a taskbar button. fk = window index.
task_toggle PROC NEAR
    mov cx,fk
    mov al,1
    shl al,cl
    mov bl,cl
    inc bl                             ; window number
    test hidden,al
    jz tt_visible
    not al                             ; restore a minimized window
    and hidden,al
    mov active,bl
    ret
tt_visible:
    cmp bl,active
    jne tt_raise
    jmp minimize_window                ; its button again: minimize
tt_raise:
    mov active,bl
    ret
task_toggle ENDP

task_click PROC NEAR
    mov ax,2                           ; Start button
    mov bx,455
    mov cx,72
    mov dx,22
    call pt_in
    jnc tk_tasks
    mov menuopen,1
    mov menuhot,0ffh
    ret
tk_tasks:
    mov fk,0
    mov slot,0
tk_loop:
    mov cx,fk
    mov al,1
    shl al,cl
    test opened,al
    jz tk_next
    mov ax,slot
    mov cx,108
    mul cx
    add ax,88
    mov bx,455
    mov cx,104
    mov dx,22
    call pt_in
    jc tk_hit
    inc slot
tk_next:
    inc fk
    cmp fk,4
    jb tk_loop
    ret
tk_hit:
    jmp task_toggle
task_click ENDP

; Desktop icons: click selects, a second click within ~1.5 s opens.
icon_click PROC NEAR
    mov fk,0
ic_loop:
    mov ax,fk
    mov cx,80
    mul cx
    add ax,16
    mov bx,ax
    mov ax,12
    mov cx,64
    mov dx,58
    call pt_in
    jc ic_hit
    inc fk
    cmp fk,4
    jb ic_loop
    mov iconsel,0
    ret
ic_hit:
    push es
    mov ax,40h
    mov es,ax
    mov ax,es:[6ch]                    ; BIOS tick count
    pop es
    mov bx,ax
    mov cl,byte ptr fk
    inc cl                             ; icon number 1..3
    cmp cl,iconsel
    jne ic_select
    sub ax,lastclk
    cmp ax,27
    ja ic_select
    mov iconsel,0
    mov al,cl
    jmp open_window
ic_select:
    mov iconsel,cl
    mov lastclk,bx
    ret
icon_click ENDP

; A press at the pointer position.
click PROC NEAR
    mov dirty,1
    mov orgx,0
    mov orgy,0
    cmp viewstate,V_START
    L_JNE cl_notstart
    mov ax,WEL_X+WEL_W-21              ; dialog close box
    mov bx,WEL_Y+5
    mov cx,16
    mov dx,14
    call pt_in
    jnc cl_welcome
    mov al,A_ENTER
    jmp do_action
cl_welcome:
    mov si,OFFSET welcome_tab
    call hit_buttons
    jmp do_action
cl_notstart:
    cmp viewstate,V_SHUTDOWN
    L_JNE cl_desktop
    mov ax,SHD_X+SHD_W-21
    mov bx,SHD_Y+5
    mov cx,16
    mov dx,14
    call pt_in
    jnc cl_shut
    mov al,A_CANCEL
    jmp do_action
cl_shut:
    mov si,OFFSET shut_tab
    call hit_buttons
    jmp do_action
cl_desktop:
    cmp viewstate,V_DESKTOP
    L_JNE cl_done
    cmp menuopen,0
    je cl_nomenu
    jmp menu_click
cl_nomenu:
    cmp my,TASK_Y
    jb cl_windows
    jmp task_click
cl_windows:
    xor bx,bx                          ; active window first, then the rest
    mov bl,active
    dec bx
    call hit_window
    L_JC cl_done
    mov bx,3
cl_more:
    call hit_window
    L_JC cl_done
    dec bx
    L_JNS cl_more
    jmp icon_click
cl_done:
    ret
click ENDP

; Keep the Start menu highlight in step with the pointer.
menu_hover PROC NEAR
    call menu_item_at
    cmp al,menuhot
    je mh_done
    mov menuhot,al
    call restore_under
    mov ax,0
    mov bx,MENU_Y
    mov cx,184
    mov dx,TASK_Y
    call repaint_area
    call cursor_show
mh_done:
    ret
menu_hover ENDP

; Keyboard: AH = scan code, AL = ASCII. Arrow keys drive the pointer so the
; desktop stays usable without a mouse.
key_input PROC NEAR
    cmp ah,4bh
    L_JNE ki_notleft
    cmp mx,8
    L_JB ki_done
    sub mx,8
    mov curdirty,1
    ret
ki_notleft:
    cmp ah,4dh
    L_JNE ki_notright
    cmp mx,SCR_W-9
    L_JA ki_done
    add mx,8
    mov curdirty,1
    ret
ki_notright:
    cmp ah,48h
    L_JNE ki_notup
    cmp my,8
    L_JB ki_done
    sub my,8
    mov curdirty,1
    ret
ki_notup:
    cmp ah,50h
    L_JNE ki_notdown
    cmp my,SCR_H-9
    L_JA ki_done
    add my,8
    mov curdirty,1
    ret
ki_notdown:
    mov dirty,1
    cmp viewstate,V_OFF
    L_JNE ki_notoff
    cmp al,13
    L_JNE ki_done
    jmp reboot
ki_notoff:
    cmp viewstate,V_SHUTDOWN
    L_JNE ki_notdialog
    cmp al,'r'
    L_JE reboot
    cmp al,'p'
    L_JE poweroff
    cmp al,27
    L_JNE ki_dialogkeys
    mov viewstate,V_DESKTOP
    ret
ki_dialogkeys:
    cmp al,13                          ; Enter presses OK
    L_JNE ki_dialogspace
    mov al,A_OK
    jmp do_action
ki_dialogspace:
    cmp al,32
    L_JE ki_click
    ret
ki_notdialog:
    cmp ah,44h                         ; F10 opens Shut Down
    L_JNE ki_notshut
    mov viewstate,V_SHUTDOWN
    mov menuopen,0
    ret
ki_notshut:
    cmp viewstate,V_START
    L_JNE ki_desktop
    cmp al,13
    L_JNE ki_startclick
    mov viewstate,V_DESKTOP
    ret
ki_startclick:
    cmp al,32
    L_JE ki_click
    ret
ki_desktop:
    cmp al,27                          ; Esc closes the Start menu
    L_JNE ki_notesc
    mov menuopen,0
    ret
ki_notesc:
    cmp ah,43h                         ; F9 toggles it
    L_JNE ki_notmenu
    xor menuopen,1
    mov menuhot,0ffh
    ret
ki_notmenu:
    cmp ah,3bh                         ; F1..F3 open Files/Notes/System
    L_JB ki_notfn
    cmp ah,3dh
    L_JA ki_notfn
    mov al,ah
    sub al,3ah
    jmp open_window
ki_notfn:
    cmp ah,3eh                         ; F4 saves
    L_JNE ki_notsave
    jmp save_note
ki_notsave:
    cmp ah,3fh                         ; F5 closes the active window
    L_JNE ki_notclose
    jmp close_window
ki_notclose:
    cmp ah,40h                         ; F6 opens the WinKit terminal
    L_JNE ki_notf6
    mov al,4
    jmp open_window
ki_notf6:
    cmp active,4                       ; the terminal has the keyboard
    L_JNE ki_notterm
    test opened,8
    L_JZ ki_notterm
    test hidden,8
    L_JNZ ki_notterm
    jmp term_key
ki_notterm:
    cmp active,2
    L_JNE ki_other
    test opened,2
    L_JZ ki_other
    test hidden,2
    L_JNZ ki_other
    cmp al,8                           ; Notes has the keyboard
    L_JNE ki_append
    cmp notelen,0
    L_JE ki_done
    dec notelen
    mov bx,notelen
    mov note[bx],0
    ret
ki_append:
    cmp al,13
    L_JE ki_store
    cmp al,32
    L_JB ki_done
    cmp al,126
    L_JA ki_done
ki_store:
    cmp notelen,240
    L_JAE ki_done
    mov bx,notelen
    mov note[bx],al
    inc notelen
    mov status,OFFSET hintnotes
    ret
ki_other:
    cmp ah,49h                         ; PgUp / PgDn page the file list
    L_JE previous_files
    cmp ah,51h
    L_JE next_files
    cmp al,'r'
    L_JNE ki_space
    jmp refresh_files
ki_space:
    cmp al,32
    L_JNE ki_done
ki_click:
    jmp click
ki_done:
    ret
key_input ENDP

; BCD real-time clock -> "h:mm AM" in clockbuf; flags a tray repaint.
check_clock PROC NEAR
    push ax
    push bx
    push cx
    push dx
    push di
    mov ah,2
    int 1ah
    jc cc_done
    cmp cx,lastclock
    je cc_done
    mov lastclock,cx
    mov dl,cl                          ; minutes (BCD)
    mov al,ch                          ; hours (BCD)
    mov bl,al
    and bl,0fh
    mov cl,4
    shr al,cl
    mov bh,10
    mul bh
    add al,bl                          ; hour 0..23
    mov dh,'A'
    cmp al,12
    jb cc_am
    mov dh,'P'
    sub al,12
cc_am:
    or al,al
    jnz cc_hour
    mov al,12
cc_hour:
    mov di,OFFSET clockbuf
    cmp al,10
    jb cc_ones
    mov byte ptr [di],'1'
    inc di
    sub al,10
cc_ones:
    add al,'0'
    mov [di],al
    inc di
    mov byte ptr [di],':'
    inc di
    mov al,dl
    mov cl,4
    shr al,cl
    add al,'0'
    mov [di],al
    inc di
    mov al,dl
    and al,0fh
    add al,'0'
    mov [di],al
    inc di
    mov byte ptr [di],' '
    inc di
    mov [di],dh
    inc di
    mov byte ptr [di],'M'
    inc di
    mov byte ptr [di],0
    mov clockdirty,1
cc_done:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret
check_clock ENDP
