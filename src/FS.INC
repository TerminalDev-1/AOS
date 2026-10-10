; FS.INC - FAT12 volume driver over BIOS INT 13h. No DOS is involved.
;
; The whole FAT and the root directory are cached in a second segment
; (FSSEG): FAT at 0, root directory at ROOT_OFS, and a file buffer at
; BUF_OFS that holds the data for file_read / file_write. Changes are
; written straight back to both FAT copies and the directory.
;
; Names are passed as NUL-terminated "NAME.EXT" strings at DS:SI.
; The volume has one level (root directory) and 512-byte clusters.

FSSEG     EQU 2000h
FAT_OFS   EQU 0
ROOT_OFS  EQU 1200h
BUF_OFS   EQU 3000h
BUF_MAX   EQU 0d000h                   ; bytes available at BUF_OFS

; Read (fs_op = 2) or write (fs_op = 3) one sector: AX = LBA, ES:BX = buffer.
; CF set on failure after three retries.
disk_rw PROC NEAR
    push ax
    push cx
    push dx
    push di
    mov di,3
dr_try:
    push ax
    xor dx,dx
    div fs_spt
    inc dl
    mov cl,dl                          ; sector number
    xor dx,dx
    div fs_heads
    mov ch,al                          ; cylinder
    mov dh,dl                          ; head
    mov dl,fs_drive
    mov ah,fs_op
    mov al,1
    int 13h
    pop ax
    jnc dr_ok
    push ax
    xor ax,ax
    int 13h                            ; reset and retry
    pop ax
    dec di
    jnz dr_try
    stc
    jmp dr_out
dr_ok:
    clc
dr_out:
    pop di
    pop dx
    pop cx
    pop ax
    ret
disk_rw ENDP

; Read the volume parameters from the boot sector the BIOS left at 7C00
; and cache the FAT and root directory.
fs_init PROC NEAR
    push es
    xor ax,ax
    mov es,ax
    mov al,es:[7c24h]
    mov fs_drive,al
    mov ax,es:[7c18h]
    mov fs_spt,ax
    mov ax,es:[7c1ah]
    mov fs_heads,ax
    mov ax,es:[7c0eh]
    mov fs_rsvd,ax
    mov ax,es:[7c16h]
    mov fs_fatsz,ax
    mov al,es:[7c10h]
    mov fs_nfat,al
    mov ax,es:[7c11h]
    mov fs_rootent,ax
    mov cl,4
    shr ax,cl                          ; 16 entries per sector
    mov fs_rootsecs,ax
    mov al,fs_nfat
    xor ah,ah
    mul fs_fatsz
    add ax,fs_rsvd
    mov fs_rootlba,ax
    add ax,fs_rootsecs
    mov fs_datalba,ax
    mov bx,es:[7c13h]
    sub bx,ax
    mov fs_nclus,bx
    mov ax,FSSEG
    mov es,ax
    mov fs_op,2
    mov fs_cnt,0
    mov fs_ptr,FAT_OFS
fi_fat:
    mov ax,fs_rsvd
    add ax,fs_cnt
    mov bx,fs_ptr
    call disk_rw
    add fs_ptr,512
    inc fs_cnt
    mov ax,fs_cnt
    cmp ax,fs_fatsz
    jb fi_fat
    mov fs_cnt,0
    mov fs_ptr,ROOT_OFS
fi_root:
    mov ax,fs_rootlba
    add ax,fs_cnt
    mov bx,fs_ptr
    call disk_rw
    add fs_ptr,512
    inc fs_cnt
    mov ax,fs_cnt
    cmp ax,fs_rootsecs
    jb fi_root
    pop es
    ret
fs_init ENDP

; Write every FAT copy and the root directory back to disk.
fs_flush PROC NEAR
    push es
    mov ax,FSSEG
    mov es,ax
    mov fs_op,3
    mov fs_f,0
ff_copy:
    mov fs_cnt,0
    mov fs_ptr,FAT_OFS
ff_fat:
    mov al,fs_f
    xor ah,ah
    mul fs_fatsz
    add ax,fs_rsvd
    add ax,fs_cnt
    mov bx,fs_ptr
    call disk_rw
    add fs_ptr,512
    inc fs_cnt
    mov ax,fs_cnt
    cmp ax,fs_fatsz
    jb ff_fat
    inc fs_f
    mov al,fs_f
    cmp al,fs_nfat
    jb ff_copy
    mov fs_cnt,0
    mov fs_ptr,ROOT_OFS
ff_root:
    mov ax,fs_rootlba
    add ax,fs_cnt
    mov bx,fs_ptr
    call disk_rw
    add fs_ptr,512
    inc fs_cnt
    mov ax,fs_cnt
    cmp ax,fs_rootsecs
    jb ff_root
    pop es
    ret
fs_flush ENDP

; ---- FAT12 entries (ES = FSSEG) ----

; AX = cluster -> AX = next cluster value.
fat_get PROC NEAR
    push bx
    push cx
    push dx
    mov bx,ax
    shr bx,1
    add bx,ax
    mov dx,es:[bx+FAT_OFS]
    test al,1
    jz fg_even
    mov cl,4
    shr dx,cl
    jmp fg_done
fg_even:
    and dx,0fffh
fg_done:
    mov ax,dx
    pop dx
    pop cx
    pop bx
    ret
fat_get ENDP

; AX = cluster, DX = 12-bit value.
fat_set PROC NEAR
    push bx
    push cx
    push dx
    mov bx,ax
    shr bx,1
    add bx,ax
    and dx,0fffh
    mov cx,es:[bx+FAT_OFS]
    test al,1
    jz fst_even
    and cx,000fh
    push cx
    mov cl,4
    shl dx,cl
    pop cx
    or cx,dx
    jmp fst_store
fst_even:
    and cx,0f000h
    or cx,dx
fst_store:
    mov es:[bx+FAT_OFS],cx
    pop dx
    pop cx
    pop bx
    ret
fat_set ENDP

; Free every cluster of the chain that entry BX (in ES) starts.
fs_free_chain PROC NEAR
    push ax
    push cx
    push dx
    mov ax,es:[bx+26]
ffc_loop:
    cmp ax,2
    jb ffc_done
    cmp ax,0ff0h
    jae ffc_done
    push ax
    call fat_get
    mov cx,ax
    pop ax
    xor dx,dx
    call fat_set
    mov ax,cx
    jmp ffc_loop
ffc_done:
    pop dx
    pop cx
    pop ax
    ret
fs_free_chain ENDP

; First free cluster -> AX; CF set when the disk is full.
fs_alloc PROC NEAR
    push cx
    mov cx,fs_nclus
    mov ax,2
fa_loop:
    push ax
    call fat_get
    or ax,ax
    pop ax
    jz fa_found
    inc ax
    loop fa_loop
    stc
    pop cx
    ret
fa_found:
    clc
    pop cx
    ret
fs_alloc ENDP

; Number of free clusters -> AX.
fs_free_count PROC NEAR
    push bx
    push cx
    push dx
    push es
    mov ax,FSSEG
    mov es,ax
    xor bx,bx
    mov cx,fs_nclus
    mov ax,2
ffr_loop:
    push ax
    call fat_get
    or ax,ax
    pop ax
    jnz ffr_used
    inc bx
ffr_used:
    inc ax
    loop ffr_loop
    mov ax,bx
    pop es
    pop dx
    pop cx
    pop bx
    ret
fs_free_count ENDP

; ---- names ----

; DS:SI "NAME.EXT" -> fsname (11 bytes, upper case, space padded).
fs_cvt PROC NEAR
    push ax
    push cx
    push si
    push di
    push es
    push ds
    pop es
    mov di,OFFSET fsname
    mov cx,11
    mov al,' '
    rep stosb
    mov di,OFFSET fsname
    mov cx,8
fcv_base:
    lodsb
    or al,al
    jz fcv_done
    cmp al,'.'
    je fcv_ext
    call to_upper
    stosb
    loop fcv_base
fcv_skip:
    lodsb
    or al,al
    jz fcv_done
    cmp al,'.'
    jne fcv_skip
fcv_ext:
    mov di,OFFSET fsname+8
    mov cx,3
fcv_e:
    lodsb
    or al,al
    jz fcv_done
    call to_upper
    stosb
    loop fcv_e
fcv_done:
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret
fs_cvt ENDP

to_upper PROC NEAR
    cmp al,'a'
    jb tu_done
    cmp al,'z'
    ja tu_done
    sub al,20h
tu_done:
    ret
to_upper ENDP

; ES:BX directory entry -> DS:DI "NAME.EXT",0
fs_unpack PROC NEAR
    push ax
    push cx
    push si
    push di
    mov si,bx
    mov cx,8
fu_base:
    mov al,es:[si]
    cmp al,' '
    je fu_pad
    mov [di],al
    inc di
    inc si
    loop fu_base
    jmp fu_ext
fu_pad:
    add si,cx
fu_ext:
    cmp byte ptr es:[si],' '
    je fu_end
    mov byte ptr [di],'.'
    inc di
    mov cx,3
fu_e:
    mov al,es:[si]
    cmp al,' '
    je fu_end
    mov [di],al
    inc di
    inc si
    loop fu_e
fu_end:
    mov byte ptr [di],0
    pop di
    pop si
    pop cx
    pop ax
    ret
fs_unpack ENDP

; ES = FSSEG. Looks up fsname: BX = entry offset, CF set when absent.
fs_find PROC NEAR
    push cx
    push si
    push di
    mov bx,ROOT_OFS
    mov cx,fs_rootent
ffn_next:
    mov al,es:[bx]
    or al,al
    jz ffn_none
    cmp al,0e5h
    je ffn_skip
    mov si,OFFSET fsname
    mov di,bx
    push cx
    mov cx,11
    repe cmpsb
    pop cx
    je ffn_hit
ffn_skip:
    add bx,32
    loop ffn_next
ffn_none:
    stc
    jmp ffn_out
ffn_hit:
    clc
ffn_out:
    pop di
    pop si
    pop cx
    ret
fs_find ENDP

; ---- files ----

; SI = name. Loads the file to FSSEG:BUF_OFS; AX = size (capped to the
; buffer). CF set when the file does not exist or cannot be read.
file_read PROC NEAR
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call fs_cvt
    mov ax,FSSEG
    mov es,ax
    call fs_find
    jc fr_fail
    mov ax,es:[bx+28]
    cmp ax,BUF_MAX
    jbe fr_size
    mov ax,BUF_MAX
fr_size:
    mov fs_size,ax
    mov ax,es:[bx+26]
    mov di,BUF_OFS
    mov fs_op,2
fr_loop:
    cmp ax,2
    jb fr_ok
    cmp ax,0ff0h
    jae fr_ok
    mov fs_cur,ax
    add ax,fs_datalba
    sub ax,2
    mov bx,di
    call disk_rw
    jc fr_fail
    add di,512
    jz fr_ok                           ; wrapped past the end of the segment
    mov ax,fs_cur
    call fat_get
    jmp fr_loop
fr_ok:
    mov ax,fs_size
    clc
    jmp fr_out
fr_fail:
    stc
fr_out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret
file_read ENDP

; SI = name, CX = size. Writes FSSEG:BUF_OFS as that file, replacing any
; existing one. CF set on failure (directory or disk full, I/O error).
file_write PROC NEAR
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov fs_size,cx
    call fs_cvt
    mov ax,FSSEG
    mov es,ax
    call fs_find
    jnc fw_have
    mov bx,ROOT_OFS                    ; new file: first free slot
    mov cx,fs_rootent
fw_slot:
    mov al,es:[bx]
    or al,al
    jz fw_new
    cmp al,0e5h
    je fw_new
    add bx,32
    loop fw_slot
    jmp fw_fail
fw_have:
    mov fs_ent,bx
    call fs_free_chain
    jmp fw_fill
fw_new:
    mov fs_ent,bx
fw_fill:
    mov di,fs_ent
    mov cx,16
    xor ax,ax
    rep stosw
    mov di,fs_ent
    mov si,OFFSET fsname
    mov cx,11
    rep movsb
    mov bx,fs_ent
    mov byte ptr es:[bx+11],20h
    mov word ptr es:[bx+16],0021h
    mov word ptr es:[bx+24],0021h
    mov ax,fs_size
    mov es:[bx+28],ax
    add ax,511
    mov cl,9
    shr ax,cl
    mov fs_n,ax                        ; clusters needed
    mov fs_prev,0
    mov fs_first,0
    mov fs_src,BUF_OFS
    mov fs_op,3
fw_alloc:
    cmp fs_n,0
    je fw_linked
    call fs_alloc
    jc fw_full
    mov fs_cur,ax
    mov dx,0fffh
    call fat_set                       ; mark as end of chain
    cmp fs_prev,0
    jne fw_link
    mov ax,fs_cur
    mov fs_first,ax
    jmp fw_data
fw_link:
    mov ax,fs_prev
    mov dx,fs_cur
    call fat_set
fw_data:
    mov ax,fs_cur
    add ax,fs_datalba
    sub ax,2
    mov bx,fs_src
    call disk_rw
    jc fw_full
    add fs_src,512
    mov ax,fs_cur
    mov fs_prev,ax
    dec fs_n
    jmp fw_alloc
fw_linked:
    mov bx,fs_ent
    mov ax,fs_first
    mov es:[bx+26],ax
    call fs_flush
    clc
    jmp fw_out
fw_full:                               ; give back what was allocated
    mov bx,fs_ent
    mov ax,fs_first
    mov es:[bx+26],ax
    call fs_free_chain
    mov byte ptr es:[bx],0e5h
fw_fail:
    stc
fw_out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret
file_write ENDP

; SI = name. CF set when it does not exist.
file_delete PROC NEAR
    push bx
    push si
    push es
    call fs_cvt
    mov ax,FSSEG
    mov es,ax
    call fs_find
    jc fd_out
    mov fs_ent,bx
    call fs_free_chain
    mov bx,fs_ent
    mov byte ptr es:[bx],0e5h
    call fs_flush
    clc
fd_out:
    pop es
    pop si
    pop bx
    ret
file_delete ENDP

; SI = old name, DI = new name. CF set when old is missing or new exists.
file_rename PROC NEAR
    push bx
    push cx
    push si
    push di
    push es
    push si
    mov si,di
    call fs_cvt                        ; new name
    push ds
    pop es
    mov si,OFFSET fsname
    mov di,OFFSET fsname2
    mov cx,11
    rep movsb
    pop si
    call fs_cvt                        ; old name
    mov ax,FSSEG
    mov es,ax
    call fs_find
    jc fn_fail
    mov fs_ent,bx
    push ds
    pop es                             ; fsname <- new name to test it
    mov si,OFFSET fsname2
    mov di,OFFSET fsname
    mov cx,11
    rep movsb
    mov ax,FSSEG
    mov es,ax
    call fs_find
    jnc fn_fail                        ; target already exists
    mov bx,fs_ent
    mov di,bx
    mov si,OFFSET fsname2
    mov cx,11
    rep movsb
    call fs_flush
    clc
    jmp fn_out
fn_fail:
    stc
fn_out:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    ret
file_rename ENDP
