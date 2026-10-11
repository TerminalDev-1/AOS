; STORAGE.INC - the File Manager's list and actions, and NOTES.TXT, on top
; of FS.INC.

; Fill files[]: 14 bytes per entry (attribute, 13-byte "NAME.EXT",0), with
; the sizes in filesize[], and count the free space. The File Manager stays
; on its page, and keeps its selection, as far as the files still go.
refresh_files PROC NEAR
    push es
    mov ax,FSSEG
    mov es,ax
    mov bx,ROOT_OFS
    mov cx,fs_rootent
    mov filecount,0
rf_next:
    mov al,es:[bx]
    or al,al
    jz rf_done
    cmp al,0e5h
    je rf_skip
    mov al,es:[bx+11]
    test al,8                          ; volume label
    jnz rf_skip
    cmp al,0fh                         ; long-name fragment
    je rf_skip
    cmp filecount,224
    jae rf_done
    push ax
    push cx
    mov ax,filecount
    mov cx,14
    mul cx
    add ax,OFFSET files
    mov di,ax
    pop cx
    pop ax
    mov [di],al                        ; attribute
    inc di
    call fs_unpack
    mov ax,es:[bx+28]                  ; size: 16 bits are shown
    cmp word ptr es:[bx+30],0
    je rf_size
    mov ax,0ffffh
rf_size:
    push bx
    mov bx,filecount
    shl bx,1
    mov filesize[bx],ax
    pop bx
    inc filecount
rf_skip:
    add bx,32
    dec cx
    jnz rf_next
rf_done:
    pop es
    mov ax,filepage                    ; back to the last page that exists
rf_page:
    cmp ax,filecount
    jb rf_paged
    or ax,ax
    jz rf_paged
    sub ax,FM_ROWS
    jmp rf_page
rf_paged:
    mov filepage,ax
    mov ax,fmsel
    cmp ax,filecount
    jb rf_sel
    mov fmsel,0ffffh
rf_sel:
    call fs_free_count
    mov fmfree,ax
    call prog_scan
    mov dirty,1
    ret
refresh_files ENDP

; List the programs in files[]: every .WKX except the Store, which has its
; own entries. progptr[] (Start > Programs) gets all of them; deskptr[]
; (desktop shortcuts) gets those without the "system" attribute, which is
; everything that was not shipped as part of WinKit, and SETUP.
prog_scan PROC NEAR
    push ax
    push bx
    push cx
    push si
    push di
    push es
    push ds
    pop es
    mov progcount,0
    mov deskcount,0
    mov cx,filecount
    mov si,OFFSET files+1
    jcxz pgs_done
pgs_next:
    mov bx,si
pgs_dot:
    mov al,[bx]
    or al,al
    jz pgs_skip
    inc bx
    cmp al,'.'
    jne pgs_dot
    cmp word ptr [bx],4b57h            ; "WK"
    jne pgs_skip
    cmp word ptr [bx+2],0058h          ; "X", end of name
    jne pgs_skip
    push si
    push cx
    mov di,OFFSET storefile
    mov cx,12
    repe cmpsb
    pop cx
    pop si
    je pgs_skip
    cmp progcount,SUB_MAX
    jae pgs_desk
    mov bx,progcount
    shl bx,1
    mov progptr[bx],si
    inc progcount
pgs_desk:
    test byte ptr [si-1],4             ; attribute: system = no shortcut
    jnz pgs_skip
    cmp deskcount,DESK_MAX
    jae pgs_skip
    mov bx,deskcount
    shl bx,1
    mov deskptr[bx],si
    inc deskcount
pgs_skip:
    add si,14
    loop pgs_next
pgs_done:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret
prog_scan ENDP

; ---- File Manager ----

; SI -> "NAME.EXT". ZF set when it is a program (.WKX). Changes AL.
fm_iswkx PROC NEAR
    push bx
    mov bx,si
fiw_dot:
    mov al,[bx]
    or al,al
    jz fiw_no
    inc bx
    cmp al,'.'
    jne fiw_dot
    cmp word ptr [bx],4b57h            ; "WK"
    jne fiw_out
    cmp word ptr [bx+2],0058h          ; "X", end of name
    jmp fiw_out
fiw_no:
    or bx,bx                           ; ZF clear
fiw_out:
    pop bx
    ret
fm_iswkx ENDP

; SI -> "NAME.EXT" -> SI = what kind of file that is, in a word.
fm_type PROC NEAR
    push ax
    push bx
    mov bx,si
    mov si,OFFSET fm_tfile
fty_dot:
    mov al,[bx]
    or al,al
    jz fty_out
    inc bx
    cmp al,'.'
    jne fty_dot
    mov ax,[bx]
    mov bl,[bx+2]
    cmp ax,4b57h                       ; "WKX"
    jne fty_txt
    cmp bl,'X'
    jne fty_out
    mov si,OFFSET fm_tprog
    jmp fty_out
fty_txt:
    cmp ax,5854h                       ; "TXT"
    jne fty_out
    cmp bl,'T'
    jne fty_out
    mov si,OFFSET fm_ttext
fty_out:
    pop bx
    pop ax
    ret
fm_type ENDP

; SI -> the name of the selected file. CF set, and a message left, when
; none is selected. Changes AX, BX, DX.
fm_selname PROC NEAR
    mov ax,fmsel
    cmp ax,filecount
    jae fsn_none
    mov bx,14
    mul bx
    add ax,OFFSET files+1
    mov si,ax
    clc
    ret
fsn_none:
    mov fmmsg,OFFSET fm_mselect
    stc
    ret
fm_selname ENDP

; A click in the list selects the file under the pointer; a second click
; on it within about 1.5 s opens it. A click below the last file selects
; nothing.
files_click PROC NEAR
    mov fmmode,0
    mov fmmsg,0
    mov ax,mx
    sub ax,wx
    cmp ax,8
    jb fcl_none
    cmp ax,WIN_W-8
    jae fcl_none
    mov ax,my
    sub ax,wy
    sub ax,79
    jb fcl_none
    mov bl,18
    div bl                             ; AL = row
    cmp al,FM_ROWS
    jae fcl_none
    xor ah,ah
    add ax,filepage
    cmp ax,filecount
    jae fcl_none
    push es
    mov bx,40h
    mov es,bx
    mov bx,es:[6ch]                    ; BIOS tick count
    pop es
    cmp ax,fmsel
    jne fcl_select
    mov cx,bx
    sub cx,fmclk
    cmp cx,27
    ja fcl_select
    jmp fm_open
fcl_select:
    mov fmsel,ax
    mov fmclk,bx
    ret
fcl_none:
    mov fmsel,0ffffh
    ret
files_click ENDP

; Open: a program is run; any other file is shown in the terminal, as
; "type NAME" typed there would.
fm_open PROC NEAR
    mov fmmode,0
    call fm_selname
    jc fop_done
    call fm_iswkx
    jne fop_text
    jmp run_named
fop_text:
    push si
    mov al,A_TERM
    call open_window
    mov si,OFFSET c_type
    call term_puts
    mov al,' '
    call term_putc
    pop si
    mov argp,si
    call term_puts
    call term_crlf
    mov cmdlen,0
    call cmd_type
    call term_prompt
    mov termdirty,1
fop_done:
    ret
fm_open ENDP

; Rename: the name is typed in the status panel, starting from the old one.
fm_ask_rename PROC NEAR
    mov fmmode,0
    call fm_selname
    jc far_done
    mov di,OFFSET fmname
    xor cx,cx
far_copy:
    lodsb
    mov [di],al
    inc di
    or al,al
    jz far_set
    inc cx
    jmp far_copy
far_set:
    mov fmlen,cx
    mov fmmode,1
    mov fmmsg,0
far_done:
    ret
fm_ask_rename ENDP

; Copy: the name of the copy is typed in the status panel.
fm_ask_copy PROC NEAR
    mov fmmode,0
    call fm_selname
    jc fac_done
    mov fmname,0
    mov fmlen,0
    mov fmmode,2
    mov fmmsg,0
fac_done:
    ret
fm_ask_copy ENDP

; Delete asks first; Y, Enter or the button again says yes.
fm_ask_delete PROC NEAR
    cmp fmmode,3
    je fm_do_delete
    mov fmmode,0
    call fm_selname
    jc fad_done
    mov fmmode,3
    mov fmmsg,0
fad_done:
    ret
fm_ask_delete ENDP

fm_do_delete PROC NEAR
    mov fmmode,0
    call fm_selname
    jc fdd_done
    call file_delete
    mov fmmsg,OFFSET fm_mdeleted
    jnc fdd_ok
    mov fmmsg,OFFSET fm_mfail
fdd_ok:
    mov fmsel,0ffffh
    call refresh_files
fdd_done:
    ret
fm_do_delete ENDP

; Enter after a name was typed: rename the selected file to it, or copy
; the file under that name. A name that is taken is refused both ways.
fm_apply PROC NEAR
    mov al,fmmode
    mov fmmode,0
    cmp fmlen,0
    je fap_done
    push ax
    call fm_selname
    pop ax
    jc fap_done
    cmp al,1
    jne fap_copy
    mov di,OFFSET fmname
    call file_rename
    mov fmmsg,OFFSET fm_minuse
    jc fap_done
    mov fmmsg,OFFSET fm_mrenamed
    jmp fap_ok
fap_copy:
    push si
    mov si,OFFSET fmname
    call fs_cvt
    push es
    mov ax,FSSEG
    mov es,ax
    call fs_find
    pop es
    pop si
    mov fmmsg,OFFSET fm_minuse
    jnc fap_done
    call file_read
    mov fmmsg,OFFSET fm_mfail
    jc fap_done
    mov cx,ax
    mov si,OFFSET fmname
    call file_write
    mov fmmsg,OFFSET fm_mfull
    jc fap_done
    mov fmmsg,OFFSET fm_mcopied
fap_ok:
    mov fmsel,0ffffh
    call refresh_files
fap_done:
    ret
fm_apply ENDP

; A key while the File Manager is the active window: AL = ASCII, AH = scan
; code. CF set when the key was its own. Enter opens the selected file and
; Delete asks to delete it; while a name is being typed or a question is
; open, every key is taken.
fm_key PROC NEAR
    cmp fmmode,0
    jne fmk_mode
    cmp al,13
    jne fmk_del
    call fm_open
    jmp fmk_used
fmk_del:
    cmp ah,53h
    jne fmk_free
    call fm_ask_delete
    jmp fmk_used
fmk_free:
    clc
    ret
fmk_mode:
    cmp fmmode,3
    jne fmk_name
    mov fmmode,0                       ; anything but yes is a no
    cmp al,13
    je fmk_yes
    call to_upper
    cmp al,'Y'
    jne fmk_used
fmk_yes:
    call fm_do_delete
    jmp fmk_used
fmk_name:
    cmp al,13
    jne fmk_bs
    call fm_apply
    jmp fmk_used
fmk_bs:
    mov bx,fmlen
    cmp al,8
    jne fmk_char
    or bx,bx
    jz fmk_used
    dec bx
    jmp fmk_store
fmk_char:
    cmp bx,12
    jae fmk_used
    call to_upper
    cmp al,'.'                         ; one dot, and not in front
    jne fmk_class
    or bx,bx
    jz fmk_used
    mov si,OFFSET fmname
fmk_dot:
    cmp byte ptr [si],0
    je fmk_put
    cmp byte ptr [si],'.'
    je fmk_used
    inc si
    jmp fmk_dot
fmk_class:
    cmp al,'_'
    je fmk_put
    cmp al,'-'
    je fmk_put
    cmp al,'0'
    jb fmk_used
    cmp al,'9'
    jbe fmk_put
    cmp al,'A'
    jb fmk_used
    cmp al,'Z'
    ja fmk_used
fmk_put:
    mov fmname[bx],al
    inc bx
fmk_store:
    mov fmname[bx],0
    mov fmlen,bx
fmk_used:
    stc
    ret
fm_key ENDP

; The Refresh button: read the disk again and tidy the status line.
fm_refresh PROC NEAR
    mov fmmode,0
    mov fmmsg,0
    jmp refresh_files
fm_refresh ENDP

; Desktop icon AX (0..4 the system's, 5.. program shortcuts): dcolx = x of
; its column, AX = y. Five icons to a column.
icon_place PROC NEAR
    push bx
    push dx
    xor dx,dx
    mov bx,5
    div bx                             ; AX = column, DX = row
    push dx
    mov bx,80
    mul bx
    mov dcolx,ax
    pop ax
    mul bx
    add ax,16
    pop dx
    pop bx
    ret
icon_place ENDP

next_files PROC NEAR
    mov ax,filepage
    add ax,FM_ROWS
    cmp ax,filecount
    jae nf_done
    mov filepage,ax
    mov dirty,1
nf_done:
    ret
next_files ENDP

previous_files PROC NEAR
    cmp filepage,0
    je pf_done
    sub filepage,FM_ROWS
    mov dirty,1
pf_done:
    ret
previous_files ENDP

; NOTES.TXT -> note[] / notelen (at most 240 bytes).
load_note PROC NEAR
    push es
    mov si,OFFSET notefile
    call file_read
    jc ln_done
    cmp ax,240
    jbe ln_size
    mov ax,240
ln_size:
    mov notelen,ax
    mov cx,ax
    push ds
    mov ax,FSSEG
    mov ds,ax
    mov si,BUF_OFS
    push cs
    pop es
    mov di,OFFSET note
    rep movsb
    pop ds
ln_done:
    pop es
    ret
load_note ENDP

save_note PROC NEAR
    push es
    mov ax,FSSEG
    mov es,ax
    mov di,BUF_OFS
    mov si,OFFSET note
    mov cx,notelen
    rep movsb
    pop es
    mov si,OFFSET notefile
    mov cx,notelen
    call file_write
    jc sn_error
    mov status,OFFSET savedtext
    jmp refresh_files                  ; the File Manager shows it at once
sn_error:
    mov status,OFFSET errortext
    mov dirty,1
    ret
save_note ENDP
