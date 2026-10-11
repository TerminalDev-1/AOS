; TERM.INC - the WinKit terminal: a 50 x 16 text screen inside a window,
; its built-in commands, and the INT 80h interface for native programs.

; ---- text screen ----

; DI = address of the cell at the cursor.
term_cell PROC NEAR
    push ax
    push cx
    push dx
    mov ax,term_row
    mov cx,52
    mul cx
    add ax,OFFSET tbuf
    add ax,term_col
    mov di,ax
    pop dx
    pop cx
    pop ax
    ret
term_cell ENDP

term_clear PROC NEAR
    push ax
    push cx
    push di
    push es
    push ds
    pop es
    mov di,OFFSET tbuf
    mov cx,16*52
    xor al,al
    rep stosb
    mov term_row,0
    mov term_col,0
    pop es
    pop di
    pop cx
    pop ax
    ret
term_clear ENDP

term_newline PROC NEAR
    push ax
    push cx
    push si
    push di
    push es
    mov term_col,0
    inc term_row
    cmp term_row,16
    jb tn_done
    push ds
    pop es
    mov si,OFFSET tbuf+52              ; scroll up one row
    mov di,OFFSET tbuf
    mov cx,15*52
    rep movsb
    mov di,OFFSET tbuf+15*52
    mov cx,52
    xor al,al
    rep stosb
    mov term_row,15
tn_done:
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret
term_newline ENDP

; AL = character (CR, LF, BS and printable ASCII are understood).
term_putc PROC NEAR
    push ax
    push di
    cmp al,13
    je tp_cr
    cmp al,10
    je tp_lf
    cmp al,8
    je tp_bs
    cmp al,32
    jb tp_done
    cmp term_col,50
    jb tp_store
    push ax
    call term_newline
    pop ax
tp_store:
    call term_cell
    mov [di],al
    inc term_col
    jmp tp_done
tp_cr:
    mov term_col,0
    jmp tp_done
tp_lf:
    call term_newline
    jmp tp_done
tp_bs:
    cmp term_col,0
    je tp_done
    dec term_col
    call term_cell
    mov byte ptr [di],0
tp_done:
    pop di
    pop ax
    ret
term_putc ENDP

; SI = NUL-terminated string (kernel segment).
term_puts PROC NEAR
    push ax
    push si
tps_next:
    lodsb
    or al,al
    jz tps_done
    call term_putc
    jmp tps_next
tps_done:
    pop si
    pop ax
    ret
term_puts ENDP

term_crlf PROC NEAR
    push ax
    mov al,13
    call term_putc
    mov al,10
    call term_putc
    pop ax
    ret
term_crlf ENDP

; AX in decimal.
term_putnum PROC NEAR
    push ax
    push bx
    push cx
    push dx
    mov bx,10
    xor cx,cx
tpn_div:
    xor dx,dx
    div bx
    push dx
    inc cx
    or ax,ax
    jnz tpn_div
tpn_out:
    pop ax
    add al,'0'
    call term_putc
    loop tpn_out
    pop dx
    pop cx
    pop bx
    pop ax
    ret
term_putnum ENDP

; AL = BCD byte as two digits.
term_bcd PROC NEAR
    push ax
    push cx
    mov ah,al
    mov cl,4
    shr al,cl
    add al,'0'
    call term_putc
    mov al,ah
    and al,0fh
    add al,'0'
    call term_putc
    pop cx
    pop ax
    ret
term_bcd ENDP

; Banner and first prompt when the system starts.
term_init PROC NEAR
    push si
    call term_clear
    mov si,OFFSET t_banner
    call term_puts
    call term_crlf
    call term_prompt
    pop si
    ret
term_init ENDP

term_prompt PROC NEAR
    push si
    mov si,OFFSET t_prompt
    call term_puts
    pop si
    ret
term_prompt ENDP

; ---- drawing (window origin in wox/woy) ----

body_term PROC NEAR
    WBOX 3,21,WIN_W-6,4,K_BLACK
    WBOX 3,281,WIN_W-6,4,K_BLACK
    mov fk,0
tb_row:
    mov ax,fk
    mov cx,16
    mul cx
    add ax,woy
    add ax,25
    mov fyy,ax
    mov ax,wox
    add ax,3
    mov rx,ax
    mov ax,fyy
    mov ry,ax
    mov rw,WIN_W-6
    mov rh,16
    mov ink,K_BLACK
    call rect
    mov ax,fk
    mov cx,52
    mul cx
    add ax,OFFSET tbuf
    mov si,ax
    cmp byte ptr [si],0
    je tb_next
    mov ax,wox
    add ax,8
    mov tx,ax
    mov ax,fyy
    mov ty,ax
    mov fg,K_FACE
    call textout
tb_next:
    inc fk
    cmp fk,16
    jb tb_row
    mov ax,term_col                        ; underscore caret
    shl ax,1
    shl ax,1
    shl ax,1
    add ax,wox
    add ax,8
    mov rx,ax
    mov ax,term_row
    mov cx,16
    mul cx
    add ax,woy
    add ax,25+14
    mov ry,ax
    mov rw,8
    mov rh,2
    mov ink,K_FACE
    call rect
    ret
body_term ENDP

; Redraw the terminal in place, without repainting the scene. Only valid
; while its window is on top and visible.
term_refresh PROC NEAR
    call restore_under
    mov ax,wx[6]
    mov wox,ax
    mov ax,wy[6]
    mov woy,ax
    call body_term
    call cursor_show
    ret
term_refresh ENDP

; Called from the main loop after the text changed.
term_update PROC NEAR
    cmp active,4
    jne tu_full
    test opened,8
    jz tu_full
    test hidden,8
    jnz tu_full
    jmp term_refresh
tu_full:
    mov dirty,1
    ret
term_update ENDP

; ---- keyboard ----

; AL = ASCII, AH = scan code, delivered while the terminal has focus.
term_key PROC NEAR
    cmp al,13
    je tk_enter
    cmp al,8
    je tk_bs
    cmp al,32
    jb tk_done
    cmp al,126
    ja tk_done
    cmp cmdlen,60
    jae tk_done
    mov bx,cmdlen
    mov cmdbuf[bx],al
    inc cmdlen
    call term_putc
    jmp tk_upd
tk_bs:
    cmp cmdlen,0
    je tk_done
    dec cmdlen
    mov al,8
    call term_putc
    jmp tk_upd
tk_enter:
    call term_crlf
    mov bx,cmdlen
    mov cmdbuf[bx],0
    mov cmdlen,0
    call term_exec
    call term_prompt
tk_upd:
    mov termdirty,1
tk_done:
    ret
term_key ENDP

; ---- command interpreter ----

; DS:SI word vs lower-case pattern DS:DI. ZF set on a match; SI is left
; after the word.
match_word PROC NEAR
    push ax
    push di
mw_loop:
    mov al,[di]
    or al,al
    jz mw_end
    mov ah,[si]
    cmp ah,'A'
    jb mw_cmp
    cmp ah,'Z'
    ja mw_cmp
    add ah,20h
mw_cmp:
    cmp al,ah
    jne mw_no
    inc si
    inc di
    jmp mw_loop
mw_end:
    mov al,[si]
    or al,al
    jz mw_yes
    cmp al,' '
    je mw_yes
mw_no:
    mov al,1
    or al,al                           ; ZF clear
    jmp mw_out
mw_yes:
    xor al,al                          ; ZF set
mw_out:
    pop di
    pop ax
    ret
match_word ENDP

; SI = text; copies the next word to DI (NUL terminated, max 14 chars) and
; leaves SI after it and any spaces.
next_arg PROC NEAR
    push ax
    push cx
    mov cx,14
na_skip:
    cmp byte ptr [si],' '
    jne na_copy
    inc si
    jmp na_skip
na_copy:
    mov al,[si]
    or al,al
    jz na_end
    cmp al,' '
    je na_end
    cmp cx,0
    je na_drop
    mov [di],al
    inc di
    dec cx
na_drop:
    inc si
    jmp na_copy
na_end:
    mov byte ptr [di],0
    pop cx
    pop ax
    ret
next_arg ENDP

term_exec PROC NEAR
    mov si,OFFSET cmdbuf
te_skip:
    cmp byte ptr [si],' '
    jne te_go
    inc si
    jmp te_skip
te_go:
    cmp byte ptr [si],0
    je te_none
    mov cmdp,si
    mov bx,OFFSET cmdtab
te_next:
    mov di,[bx]
    or di,di
    jz te_prog
    mov si,cmdp
    call match_word
    je te_found
    add bx,4
    jmp te_next
te_found:
    cmp byte ptr [si],' '              ; arguments start after the spaces
    jne te_args
    inc si
    jmp te_found
te_args:
    mov argp,si
    mov ax,[bx+2]
    jmp ax
te_none:
    ret
te_prog:
    jmp cmd_run
term_exec ENDP

cmd_help PROC NEAR
    mov si,OFFSET t_help
hp_next:
    cmp byte ptr [si],0
    je hp_done
    call term_puts
hp_skip:
    lodsb
    or al,al
    jnz hp_skip
    call term_crlf
    jmp hp_next
hp_done:
    ret
cmd_help ENDP

cmd_cls PROC NEAR
    call term_clear
    ret
cmd_cls ENDP

cmd_ver PROC NEAR
    mov si,OFFSET t_ver
    call term_puts
    call term_crlf
    ret
cmd_ver ENDP

cmd_time PROC NEAR
    mov ah,2
    int 1ah
    mov al,ch
    call term_bcd
    mov al,':'
    call term_putc
    mov al,cl
    call term_bcd
    mov al,':'
    call term_putc
    mov al,dh
    call term_bcd
    call term_crlf
    ret
cmd_time ENDP

cmd_mem PROC NEAR
    int 12h
    call term_putnum
    mov si,OFFSET t_mem
    call term_puts
    call term_crlf
    ret
cmd_mem ENDP

cmd_reboot PROC NEAR
    jmp reboot
cmd_reboot ENDP

cmd_exit PROC NEAR
    mov cl,3
    mov al,1
    shl al,cl
    not al
    and opened,al
    and hidden,al
    mov dirty,1
    ret
cmd_exit ENDP

cmd_dir PROC NEAR
    push es
    mov ax,FSSEG
    mov es,ax
    mov bx,ROOT_OFS
    mov cx,fs_rootent
    mov dirn,0
cd_loop:
    mov al,es:[bx]
    or al,al
    jz cd_end
    cmp al,0e5h
    je cd_next
    test byte ptr es:[bx+11],8
    jnz cd_next
    mov di,OFFSET fnbuf
    call fs_unpack
    mov si,OFFSET fnbuf
    call term_puts
    mov si,OFFSET fnbuf                ; pad the name to 14 columns
    push cx
    call strlen
    mov ax,14
    sub ax,cx
    mov cx,ax
cd_pad:
    mov al,' '
    call term_putc
    loop cd_pad
    pop cx
    mov ax,es:[bx+28]
    call term_putnum
    call term_crlf
    inc dirn
cd_next:
    add bx,32
    dec cx
    jnz cd_loop
cd_end:
    pop es
    mov ax,dirn
    call term_putnum
    mov si,OFFSET t_files
    call term_puts
    call fs_free_count
    shr ax,1                           ; clusters are 512 bytes
    call term_putnum
    mov si,OFFSET t_free
    call term_puts
    call term_crlf
    ret
cmd_dir ENDP

cmd_type PROC NEAR
    mov si,argp
    cmp byte ptr [si],0
    je ct_usage
    mov di,OFFSET arg1
    call next_arg
    mov si,OFFSET arg1
    call file_read
    jc ct_missing
    mov cx,ax
    cmp cx,1500
    jbe ct_show
    mov cx,1500
ct_show:
    or cx,cx
    jz ct_end
    push es
    mov ax,FSSEG
    mov es,ax
    mov bx,BUF_OFS
ct_loop:
    mov al,es:[bx]
    inc bx
    cmp al,13
    je ct_skip
    cmp al,10
    jne ct_char
    call term_crlf
    jmp ct_skip
ct_char:
    cmp al,32
    jb ct_dot
    cmp al,126
    jbe ct_put
ct_dot:
    mov al,'.'
ct_put:
    call term_putc
ct_skip:
    loop ct_loop
    pop es
ct_end:
    call term_crlf
    ret
ct_usage:
    mov si,OFFSET t_usage_type
    jmp ct_say
ct_missing:
    mov si,OFFSET t_nofile
ct_say:
    call term_puts
    call term_crlf
    ret
cmd_type ENDP

cmd_echo PROC NEAR
    mov si,argp
ce_scan:
    mov al,[si]
    or al,al
    jz ce_plain
    cmp al,'>'
    je ce_redir
    inc si
    jmp ce_scan
ce_plain:
    mov si,argp
    call term_puts
    call term_crlf
    ret
ce_redir:
    mov byte ptr [si],0                ; end of the text
    mov di,si
ce_trim:
    cmp di,argp
    jbe ce_name
    dec di
    cmp byte ptr [di],' '
    jne ce_name
    mov byte ptr [di],0
    jmp ce_trim
ce_name:
    inc si
    mov di,OFFSET arg1
    call next_arg
    cmp arg1,0
    je ce_usage
    push es                            ; text + CR LF into the file buffer
    mov ax,FSSEG
    mov es,ax
    mov di,BUF_OFS
    mov si,argp
ce_copy:
    lodsb
    or al,al
    jz ce_tail
    stosb
    jmp ce_copy
ce_tail:
    mov al,13
    stosb
    mov al,10
    stosb
    mov cx,di
    sub cx,BUF_OFS
    pop es
    mov si,OFFSET arg1
    call file_write
    jc ce_fail
    ret
ce_usage:
    mov si,OFFSET t_usage_echo
    jmp ce_say
ce_fail:
    mov si,OFFSET t_diskerr
ce_say:
    call term_puts
    call term_crlf
    ret
cmd_echo ENDP

cmd_copy PROC NEAR
    mov si,argp
    mov di,OFFSET arg1
    call next_arg
    mov di,OFFSET arg2
    call next_arg
    cmp arg2,0
    je cc_usage
    mov si,OFFSET arg1
    call file_read
    jc cc_missing
    mov cx,ax
    mov si,OFFSET arg2
    call file_write
    jc cc_fail
    jmp refresh_files
cc_usage:
    mov si,OFFSET t_usage_copy
    jmp cc_say
cc_missing:
    mov si,OFFSET t_nofile
    jmp cc_say
cc_fail:
    mov si,OFFSET t_diskerr
cc_say:
    call term_puts
    call term_crlf
    ret
cmd_copy ENDP

cmd_del PROC NEAR
    mov si,argp
    cmp byte ptr [si],0
    je cdl_usage
    mov di,OFFSET arg1
    call next_arg
    mov si,OFFSET arg1
    call file_delete
    jc cdl_missing
    jmp refresh_files
cdl_usage:
    mov si,OFFSET t_usage_del
    jmp cdl_say
cdl_missing:
    mov si,OFFSET t_nofile
cdl_say:
    call term_puts
    call term_crlf
    ret
cmd_del ENDP

cmd_ren PROC NEAR
    mov si,argp
    mov di,OFFSET arg1
    call next_arg
    mov di,OFFSET arg2
    call next_arg
    cmp arg2,0
    je cr_usage
    mov si,OFFSET arg1
    mov di,OFFSET arg2
    call file_rename
    jc cr_fail
    jmp refresh_files
cr_usage:
    mov si,OFFSET t_usage_ren
    jmp cr_say
cr_fail:
    mov si,OFFSET t_renfail
cr_say:
    call term_puts
    call term_crlf
    ret
cmd_ren ENDP

; Anything that is not a built-in is the name of a program: NAME or
; NAME.WKX is loaded to 3000:0100 and run.
cmd_run PROC NEAR
    mov si,cmdp
    mov di,OFFSET arg1
    call next_arg
    mov si,OFFSET arg1                 ; no dot: add the extension
cr_dot:
    mov al,[si]
    or al,al
    jz cr_addext
    cmp al,'.'
    je cr_load
    inc si
    jmp cr_dot
cr_addext:
    mov di,si
    mov si,OFFSET t_wkx
cr_ext:
    lodsb
    mov [di],al
    inc di
    or al,al
    jnz cr_ext
cr_load:
    mov si,OFFSET arg1
    call run_file
    jc cr_bad
    mov termdirty,1
    cmp filesaved,0                    ; it may have installed a program
    je cr_ran
    mov filesaved,0
    call refresh_files
cr_ran:
    ret
cr_bad:
    mov si,OFFSET t_badcmd
    call term_puts
    call term_crlf
    ret
cmd_run ENDP

; SI = file name: load it to 3000:0100 and run it. CF set when there is no
; such program.
run_file PROC NEAR
    mov chainfile,0
rnf_load:
    call file_read
    jc rnf_bad
    mov cx,ax
    cmp cx,0f000h
    ja rnf_bad
    push ds                            ; copy the image to 3000:0100
    push es
    mov ax,FSSEG
    mov ds,ax
    mov si,BUF_OFS
    mov ax,PROGSEG
    mov es,ax
    mov di,100h
    rep movsb
    xor di,di                          ; exit stub at 0: xor ah,ah / int 80h
    mov al,30h
    stosb
    mov al,0e4h
    stosb
    mov al,0cdh
    stosb
    mov al,80h
    stosb
    pop es
    pop ds
    call prog_run
    cmp chainfile,0                    ; it asked for another program
    je rnf_ok
    mov si,OFFSET chainfile
    mov di,OFFSET arg2
    mov cx,16
    rep movsb
    mov chainfile,0
    call paint                         ; a tidy desktop in between
    mov si,OFFSET arg2
    jmp rnf_load
rnf_ok:
    clc
    ret
rnf_bad:
    stc
    ret
run_file ENDP

; ---- native program interface ----

; Jumps into the program with all segments at PROGSEG. Comes back when the
; program exits (AH=0 or a RET to offset 0).
prog_run PROC NEAR
    mov kss,ss
    mov ksp,sp
    cli
    mov ax,PROGSEG
    mov ds,ax
    mov es,ax
    mov ss,ax
    mov sp,0fffeh
    sti
    xor ax,ax
    push ax                            ; RET goes to the exit stub
    xor bx,bx
    xor cx,cx
    xor dx,dx
    xor si,si
    xor di,di
    xor bp,bp
    db 0eah                            ; jmp 3000:0100
    dw 0100h,PROGSEG
prog_return:
    ret
prog_run ENDP

; INT 80h: AH = 0 exit, 1 print AL, 2 print DS:DX, 3 wait for a key,
; 4 save CX bytes at DS:SI as the file named at DS:DX, 5 exit and run the
; program named at DS:DX. 10h..15h give the program a window: see PGUI.INC.
sys_call PROC FAR
    sti
    cmp ah,0
    L_JE sys_exit
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push ds
    push es
    mov bp,sp
    push cs
    pop ds
    push cs
    pop es
    mov ax,[bp+16]                     ; caller's AX
    cmp ah,1
    je sys_putc
    cmp ah,2
    je sys_puts
    cmp ah,3
    je sys_key
    cmp ah,4
    L_JE sys_save
    cmp ah,5
    L_JE sys_chain
    cmp ah,10h
    L_JE sys_gopen
    cmp ah,11h
    L_JE sys_grect
    cmp ah,12h
    L_JE sys_gtext
    cmp ah,13h
    L_JE sys_gbevel
    cmp ah,14h
    L_JE sys_gevent
    cmp ah,15h
    L_JE sys_gpoll
    jmp sys_back
sys_putc:
    call term_putc
    jmp sys_show
sys_puts:
    mov si,[bp+10]                     ; DX
    mov es,[bp+2]                      ; caller's DS
sp_next:
    mov al,es:[si]
    inc si
    or al,al
    jz sp_done
    call term_putc
    jmp sp_next
sp_done:
    push cs
    pop es
    jmp sys_show
sys_key:
    call term_refresh_if_top
sk_wait:
    mov ah,1
    int 16h
    jnz sk_got
    sti
    hlt
    jmp sk_wait
sk_got:
    mov ah,0
    int 16h
    mov [bp+16],ax                     ; return the key in AX
    jmp sys_back
sys_show:
    call term_refresh_if_top
sys_back:
    pop es
    pop ds
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    iret
sys_exit:
    mov ax,cs
    mov ds,ax
    mov es,ax
    cli
    mov ss,kss
    mov sp,ksp
    sti
    cmp guiopen,0                      ; its window goes with it
    je se_plain
    mov guiopen,0
    call gui_fullclip
    mov dirty,1
se_plain:
    jmp prog_return
sys_save:
    mov word ptr [bp+16],1             ; AX = 1 unless the file is written
    mov es,[bp+2]                      ; caller's DS
    mov si,[bp+10]                     ; DX = name, copied to arg1
    mov di,OFFSET arg1
    mov cx,23
ss_name:
    mov al,es:[si]
    inc si
    mov [di],al
    inc di
    or al,al
    jz ss_named
    loop ss_name
    mov byte ptr [di],0
ss_named:
    cmp arg1,0
    je ss_done
    mov cx,[bp+12]                     ; CX = size
    cmp cx,BUF_MAX
    ja ss_done
    push ds                            ; caller's DS:SI -> the file buffer
    mov si,[bp+8]
    mov di,BUF_OFS
    mov ax,FSSEG
    mov es,ax
    mov ds,[bp+2]
    rep movsb
    pop ds
    mov si,OFFSET arg1
    mov cx,[bp+12]
    call file_write
    jc ss_done
    mov word ptr [bp+16],0
    mov filesaved,1
ss_done:
    push cs
    pop es
    jmp sys_back
sys_chain:
    mov word ptr [bp+16],1             ; AX = 1: there is no such program
    mov es,[bp+2]
    mov si,[bp+10]                     ; DX = name
    mov di,OFFSET arg1
    mov cx,13
    call gui_copy
    mov si,OFFSET arg1
    call fs_cvt
    mov ax,FSSEG
    mov es,ax
    call fs_find
    push cs
    pop es
    L_JC sys_back
    mov si,OFFSET arg1                 ; run_file picks it up after the exit
    mov di,OFFSET chainfile
    mov cx,14
    rep movsb
    jmp sys_exit
sys_call ENDP

term_refresh_if_top PROC NEAR
    cmp guiopen,0                      ; a program's window covers it
    jne trt_no
    cmp active,4
    jne trt_raise
    test opened,8
    jz trt_raise
    test hidden,8
    jnz trt_raise
    call term_refresh
trt_no:
    ret
trt_raise:                             ; started from the desktop: a program
    mov al,4                           ; that prints gets the terminal
    call open_window
    call paint
    ret
term_refresh_if_top ENDP

; Point INT 80h at sys_call.
sys_install PROC NEAR
    push es
    xor ax,ax
    mov es,ax
    cli
    mov word ptr es:[200h],OFFSET sys_call
    mov es:[202h],cs
    sti
    pop es
    ret
sys_install ENDP
