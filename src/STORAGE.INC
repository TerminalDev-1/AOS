; STORAGE.INC - the Files list and NOTES.TXT, on top of FS.INC.

; Fill files[]: 14 bytes per entry (attribute, 13-byte "NAME.EXT",0).
refresh_files PROC NEAR
    push es
    mov ax,FSSEG
    mov es,ax
    mov bx,ROOT_OFS
    mov cx,fs_rootent
    mov filecount,0
    mov filepage,0
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
    inc filecount
rf_skip:
    add bx,32
    dec cx
    jnz rf_next
rf_done:
    pop es
    call prog_scan
    mov dirty,1
    ret
refresh_files ENDP

; List the programs in files[] for the Start menu: every .WKX except the
; Store, which has its own entry. progptr[] points at their names.
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
    mov cx,filecount
    mov si,OFFSET files+1
    jcxz pgs_done
pgs_next:
    cmp progcount,SUB_MAX
    jae pgs_done
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
    mov bx,progcount
    shl bx,1
    mov progptr[bx],si
    inc progcount
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

; A click in the Files list: a .WKX program under the pointer is run.
files_click PROC NEAR
    mov ax,mx
    sub ax,wx
    sub ax,12
    jb fcl_done
    xor dx,dx
    mov bx,200
    div bx                             ; AX = column, DX = x inside it
    cmp ax,2
    jae fcl_done
    cmp dx,150
    jae fcl_done
    mov cx,ax
    mov ax,my
    sub ax,wy
    sub ax,64
    jb fcl_done
    mov bl,18
    div bl                             ; AL = row
    cmp al,10
    jae fcl_done
    xor ah,ah
    jcxz fcl_index
    add ax,10                          ; second column
fcl_index:
    add ax,filepage
    cmp ax,filecount
    jae fcl_done
    mov bx,14
    mul bx
    add ax,OFFSET files+1
    mov si,ax                          ; SI -> "NAME.EXT"
    mov bx,si
fcl_dot:
    mov al,[bx]
    or al,al
    jz fcl_done
    inc bx
    cmp al,'.'
    jne fcl_dot
    cmp word ptr [bx],4b57h            ; "WK"
    jne fcl_done
    cmp word ptr [bx+2],0058h          ; "X", end of name
    jne fcl_done
    jmp run_named
fcl_done:
    ret
files_click ENDP

next_files PROC NEAR
    mov ax,filepage
    add ax,20
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
    sub filepage,20
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
    mov dirty,1
    ret
sn_error:
    mov status,OFFSET errortext
    mov dirty,1
    ret
save_note ENDP
