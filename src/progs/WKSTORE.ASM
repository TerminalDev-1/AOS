; WKSTORE.WKX - the WinKit Store. A window that lists the programs a store
; server offers, installs the one you pick onto the disk as NAME.WKX, and
; runs it.
;
; WinKit has no network, so the store is reached over the serial port
; (COM1, 115200 8N1, polled). The server is scripts/wkstore.py on the host.
; The store asks, the server answers:
;
;   'L' 10          the catalogue: REC_SIZE-byte records
;   'G' NAME 10     the program NAME
;
; An answer is 'K', a length word, that many bytes and the 16-bit sum of
; those bytes; or the single byte 'E' when the server cannot do it.
;
; The window is drawn with INT 80h functions 10h..14h; coordinates are in
; pixels inside the 410 x 264 client area of the window.
CODE SEGMENT PARA PUBLIC 'CODE'
ASSUME CS:CODE, DS:CODE
ORG 100h

REC_NAME EQU 9                         ; name, NUL padded
REC_DESC EQU 31                        ; description, NUL padded
REC_SIZE EQU REC_NAME+2+REC_DESC       ; the size word sits between them
MAX_APPS EQU 64
CAT_MAX  EQU MAX_APPS*REC_SIZE
MAX_FILE EQU 0d000h                    ; the largest file WinKit can hold
PAGE     EQU 10                        ; rows in the list
TIMEOUT  EQU 55                        ; BIOS ticks, about three seconds

K_BLACK  EQU 0                         ; the WinKit palette
K_NAVY   EQU 1
K_FACE   EQU 4
K_SHADOW EQU 5
K_WHITE  EQU 6
BV_BUTTON EQU 0                        ; bevel styles
BV_FIELD  EQU 2
BV_PANEL  EQU 3

LIST_Y   EQU 43                        ; first row of the list
ROW_H    EQU 18
BAR_X    EQU 252                       ; progress bar in the status panel
BAR_W    EQU 150

; Filled rectangle.
GBOX MACRO x,y,w,h,c
    mov bx,x
    mov cx,y
    mov si,w
    mov di,h
    mov al,c
    mov ah,11h
    int 80h
ENDM

; 3D box in bevel style m.
GBEV MACRO x,y,w,h,m
    mov bx,x
    mov cx,y
    mov si,w
    mov di,h
    mov al,m
    mov ah,13h
    int 80h
ENDM

; The string s at x,y in colour c.
GTXT MACRO x,y,c,s
    mov bx,x
    mov cx,y
    mov dx,OFFSET s
    mov al,c
    mov ah,12h
    int 80h
ENDM

start:
    mov status,OFFSET t_asking
    call draw_all
    call com_init
    jnc reload
    mov status,OFFSET t_noport
    call draw_status
    jmp events
reload:
    mov pagetop,0
    mov sel,0
    mov count,0
    mov status,OFFSET t_asking
    call draw_all
    call fetch_cat
    jc rl_error
    mov status,OFFSET t_hint
    cmp count,0
    jne rl_show
    mov status,OFFSET t_empty
    jmp rl_show
rl_error:
    call set_error
rl_show:
    call draw_list
    call draw_status
events:
    mov ah,14h                         ; wait for a key or a click
    int 80h
    cmp ax,1
    je on_key
    cmp ax,2
    jne ev_other
    jmp on_click
ev_other:
    cmp ax,3                           ; the close button
    jne events
quit:
    xor ah,ah                          ; exit
    int 80h

; ---- keyboard: BL = ASCII, BH = scan code ----
on_key:
    mov al,bl
    cmp al,'a'
    jb ok_up
    cmp al,'z'
    ja ok_up
    sub al,20h
ok_up:
    cmp al,27
    je quit
    cmp al,'Q'
    je quit
    cmp al,'R'
    je reload
    cmp al,13
    je go_install
    cmp al,'I'
    je go_install
    cmp al,' '
    je go_run
    cmp bh,48h                         ; up
    jne ok_down
    cmp sel,0
    je events
    dec sel
    jmp moved
ok_down:
    cmp bh,50h                         ; down
    jne ok_pgup
    mov ax,sel
    inc ax
    cmp ax,count
    jae events
    mov sel,ax
    jmp moved
ok_pgup:
    cmp bh,49h
    je go_prev
    cmp bh,51h
    je go_next
    jmp events
go_install:
    call install
    jmp events
go_run:
    call run_it
    jmp events
go_prev:
    cmp pagetop,PAGE
    jb to_events
    mov ax,pagetop
    sub ax,PAGE
    mov sel,ax
    jmp moved
go_next:
    mov ax,pagetop
    add ax,PAGE
    cmp ax,count
    jae to_events
    mov sel,ax
moved:                                 ; the page follows the selection
    mov ax,sel
    xor dx,dx
    mov bx,PAGE
    div bx
    mul bx
    mov pagetop,ax
    call draw_list
to_events:
    jmp events

; ---- mouse: BX,CX = x,y in the client area ----
on_click:
    cmp cx,4                           ; the toolbar
    jb to_events
    cmp cx,28
    jae oc_list
    cmp bx,7
    jb to_events
    cmp bx,79
    jb go_install
    cmp bx,85
    jb to_events
    cmp bx,133
    jb go_run
    cmp bx,139
    jb to_events
    cmp bx,203
    jae oc_paging
    jmp reload
oc_paging:
    cmp bx,209
    jb to_events
    cmp bx,237
    jb go_prev
    cmp bx,243
    jb to_events
    cmp bx,271
    jb go_next
    jmp events
oc_list:
    cmp bx,5
    jb to_events
    cmp bx,405
    jae to_events
    sub cx,LIST_Y
    jb to_events
    mov ax,cx
    mov cl,ROW_H
    div cl                             ; AL = row
    xor ah,ah
    cmp ax,PAGE
    jae to_events
    add ax,pagetop
    cmp ax,count
    jae to_events
    mov sel,ax
    call draw_list
    jmp events

; ---- drawing ----

draw_all:
    mov dx,OFFSET t_title              ; a fresh, empty window
    mov ah,10h
    int 80h
    GBEV 7,4,72,24,BV_BUTTON
    GTXT 15,8,K_BLACK,t_install
    GBEV 85,4,48,24,BV_BUTTON
    GTXT 97,8,K_BLACK,t_run
    GBEV 139,4,64,24,BV_BUTTON
    GTXT 147,8,K_BLACK,t_reload
    GBEV 209,4,28,24,BV_BUTTON
    GTXT 219,8,K_BLACK,t_prev
    GBEV 243,4,28,24,BV_BUTTON
    GTXT 253,8,K_BLACK,t_next
    GBOX 0,31,410,1,K_SHADOW
    GBOX 0,32,410,1,K_WHITE
    call draw_list
    jmp draw_status

; The white list with one row per program: NAME  SIZE  description.
draw_list:
    GBEV 3,37,404,190,BV_FIELD
    GBOX 280,4,128,24,K_FACE           ; "Page n/m"
    mov di,OFFSET line
    mov si,OFFSET t_page
    call put_str
    mov ax,pagetop
    xor dx,dx
    mov bx,PAGE
    div bx
    inc ax
    call put_dec
    mov al,'/'
    stosb
    mov ax,count
    add ax,PAGE-1
    xor dx,dx
    div bx
    or ax,ax
    jnz dl_pages
    inc ax
dl_pages:
    call put_dec
    xor al,al
    stosb
    GTXT 288,8,K_BLACK,line
    mov row,0
dl_row:
    mov ax,pagetop
    add ax,row
    cmp ax,count
    jb dl_have
    ret
dl_have:
    mov ink,K_BLACK
    cmp ax,sel
    jne dl_text
    push ax
    mov ax,row                         ; highlight the selected row
    mov cx,ROW_H
    mul cx
    add ax,LIST_Y-1
    mov cx,ax
    mov bx,5
    mov si,400
    mov di,ROW_H
    mov al,K_NAVY
    mov ah,11h
    int 80h
    pop ax
    mov ink,K_WHITE
dl_text:
    mov bx,REC_SIZE
    mul bx
    add ax,OFFSET heap
    mov si,ax                          ; SI -> the record
    mov di,OFFSET line
    mov cx,REC_NAME
    call copy_pad
    mov ax,[si+REC_NAME]
    call fmt_num
    mov al,' '
    stosb
    stosb
    add si,REC_NAME+2
    mov cx,REC_DESC-1
    call copy_pad
    xor al,al
    stosb
    mov ax,row
    mov cx,ROW_H
    mul cx
    add ax,LIST_Y
    mov cx,ax
    mov bx,12
    mov dx,OFFSET line
    mov al,ink
    mov ah,12h
    int 80h
    inc row
    cmp row,PAGE
    jae dl_done
    jmp dl_row
dl_done:
    ret

; The status panel shows the string that status points to.
draw_status:
    GBEV 3,238,404,20,BV_PANEL
    mov bx,9
    mov cx,240
    mov dx,status
    mov al,K_BLACK
    mov ah,12h
    int 80h
    ret

; Called while a program arrives: CX bytes of rlen are still to come.
progress:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ax,rlen
    sub ax,cx
    mov al,ah                          ; received / 256
    xor ah,ah
    mov bx,BAR_W
    mul bx
    mov bx,rlen
    mov bl,bh
    xor bh,bh
    inc bx                             ; total / 256, never 0
    div bx
    mov si,ax
    mov bx,BAR_X
    mov cx,242
    mov di,12
    mov al,K_NAVY
    mov ah,11h
    int 80h
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; ---- text helpers (DI = where to write, moves on) ----

; Append the NUL-terminated string at SI.
put_str:
    push ax
ps_next:
    lodsb
    or al,al
    jz ps_done
    stosb
    jmp ps_next
ps_done:
    pop ax
    ret

; Append AX in decimal.
put_dec:
    push ax
    push bx
    push cx
    push dx
    mov bx,10
    xor cx,cx
pd_div:
    xor dx,dx
    div bx
    push dx
    inc cx
    or ax,ax
    jnz pd_div
pd_out:
    pop ax
    add al,'0'
    stosb
    loop pd_out
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; AX in decimal, right aligned in five characters.
fmt_num:
    push ax
    push bx
    push cx
    push dx
    add di,5
    push di
    mov bx,10
    mov cx,5
fn_digit:
    dec di
    xor dx,dx
    div bx
    add dl,'0'
    mov [di],dl
    dec cx
    or ax,ax
    jnz fn_digit
    jcxz fn_done
fn_pad:
    dec di
    mov byte ptr [di],' '
    loop fn_pad
fn_done:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Append the string at SI, padded with spaces to CX characters. SI is kept.
copy_pad:
    push ax
    push si
cp_next:
    mov al,[si]
    or al,al
    jz cp_pad
    inc si
    stosb
    loop cp_next
    jmp cp_done
cp_pad:
    mov al,' '
    rep stosb
cp_done:
    pop si
    pop ax
    ret

; AL = error number from recv_reply: point status at its message.
set_error:
    push bx
    mov bl,al
    xor bh,bh
    dec bx
    shl bx,1
    mov bx,errtab[bx]
    mov status,bx
    pop bx
    ret

; ---- serial port ----

; Finds COM1 and sets it to 115200 8N1 without interrupts. CF set when the
; machine has no serial port.
com_init:
    push es
    mov ax,40h
    mov es,ax
    mov ax,es:[0]                      ; BIOS: I/O address of COM1
    pop es
    mov port,ax
    or ax,ax
    jnz ci_set
    stc
    ret
ci_set:
    mov dx,ax
    add dx,3
    mov al,80h                         ; divisor latch
    out dx,al
    mov dx,port
    mov al,1                           ; 115200 baud
    out dx,al
    inc dx
    xor al,al
    out dx,al
    add dx,2
    mov al,3                           ; 8 data bits, no parity, 1 stop
    out dx,al
    mov dx,port
    inc dx
    xor al,al                          ; no interrupts: the port is polled
    out dx,al
    inc dx
    mov al,7                           ; FIFO on and emptied
    out dx,al
    add dx,2
    mov al,3                           ; DTR and RTS
    out dx,al
    clc
    ret

; DX = low word of the BIOS tick count (18.2 per second).
get_ticks:
    push ax
    push cx
    xor ah,ah
    int 1ah
    pop cx
    pop ax
    ret

; AL = the next byte from the server. CF set when none came in time.
com_get:
    push bx
    push cx
    push dx
    mov dx,port
    add dx,5
    mov cx,500
cg_quick:
    in al,dx
    test al,1
    jnz cg_have
    loop cg_quick
    call get_ticks                     ; nothing yet: start the clock
    mov bx,dx
cg_wait:
    mov dx,port
    add dx,5
    mov cx,500
cg_poll:
    in al,dx
    test al,1
    jnz cg_have
    loop cg_poll
    call get_ticks
    sub dx,bx
    cmp dx,TIMEOUT
    jb cg_wait
    stc
    jmp cg_out
cg_have:
    mov dx,port
    in al,dx
    clc
cg_out:
    pop dx
    pop cx
    pop bx
    ret

; Sends AL.
com_put:
    push ax
    push cx
    push dx
    mov ah,al
    mov dx,port
    add dx,5
    xor cx,cx
cu_ready:
    in al,dx
    test al,20h                        ; transmitter empty?
    jnz cu_send
    loop cu_ready
cu_send:
    mov dx,port
    mov al,ah
    out dx,al
    pop dx
    pop cx
    pop ax
    ret

; Throws away anything left over from an earlier answer.
com_flush:
    push ax
    push cx
    push dx
    mov cx,4096
cf_next:
    mov dx,port
    add dx,5
    in al,dx
    test al,1
    jz cf_done
    mov dx,port
    in al,dx
    loop cf_next
cf_done:
    pop dx
    pop cx
    pop ax
    ret

; Reads one answer into the buffer at DI, which holds BX bytes.
; CF clear: CX = length. CF set: AL = 1 no answer, 2 refused, 3 too large,
; 4 damaged.
recv_reply:
    call com_get
    jc rr_silent
    cmp al,'K'
    jne rr_refused
    call com_get
    jc rr_silent
    mov cl,al
    call com_get
    jc rr_silent
    mov ch,al
    cmp cx,bx
    ja rr_large
    mov rlen,cx
    xor dx,dx                          ; DX = sum of the bytes
    jcxz rr_sum
rr_data:
    call com_get
    jc rr_silent
    stosb
    xor ah,ah
    add dx,ax
    cmp dots,0
    je rr_quiet
    test cx,03ffh                      ; move the bar every 1 KB
    jnz rr_quiet
    call progress
rr_quiet:
    loop rr_data
rr_sum:
    call com_get
    jc rr_silent
    mov bl,al
    call com_get
    jc rr_silent
    mov bh,al
    cmp bx,dx
    jne rr_damaged
    mov cx,rlen
    clc
    ret
rr_silent:
    mov al,1
    stc
    ret
rr_refused:
    mov al,2
    stc
    ret
rr_large:
    mov al,3
    stc
    ret
rr_damaged:
    mov al,4
    stc
    ret

; ---- the store ----

; Asks for the catalogue. CF set (AL = error) when it could not be read.
fetch_cat:
    call com_flush
    mov al,'L'
    call com_put
    mov al,10
    call com_put
    mov di,OFFSET heap
    mov bx,CAT_MAX
    call recv_reply
    jc fc_out
    mov ax,cx
    xor dx,dx
    mov bx,REC_SIZE
    div bx
    mov count,ax
    mov cx,ax                          ; make sure every string ends
    jcxz fc_ok
    mov bx,OFFSET heap
fc_end:
    mov byte ptr [bx+REC_NAME-1],0
    mov byte ptr [bx+REC_SIZE-1],0
    add bx,REC_SIZE
    loop fc_end
fc_ok:
    clc
fc_out:
    ret

; Download the selected program and save it as NAME.WKX.
install:
    mov ax,sel
    cmp ax,count
    jb in_go
    ret
in_go:
    mov bx,REC_SIZE
    mul bx
    add ax,OFFSET heap
    mov recp,ax
    mov di,OFFSET msg                  ; "Downloading NAME..."
    mov si,OFFSET t_getting
    call put_str
    mov si,recp
    call put_str
    mov si,OFFSET t_dots
    call put_str
    xor al,al
    stosb
    mov status,OFFSET msg
    call draw_status
    call com_flush
    mov al,'G'
    call com_put
    mov si,recp
in_name:
    lodsb
    or al,al
    jz in_sent
    call com_put
    jmp in_name
in_sent:
    mov al,10
    call com_put
    mov dots,1
    mov di,OFFSET heap+CAT_MAX
    mov bx,MAX_FILE
    call recv_reply
    mov dots,0
    jc in_error
    mov al,2                           ; an empty file is no program
    jcxz in_error
    mov flen,cx
    mov si,recp                        ; file name: NAME.WKX
    mov di,OFFSET fname
    call put_str
    mov si,OFFSET t_ext
    call put_str
    xor al,al
    stosb
    mov dx,OFFSET fname
    mov si,OFFSET heap+CAT_MAX
    mov cx,flen
    mov ah,4                           ; save CX bytes at DS:SI as DS:DX
    int 80h
    or ax,ax
    jnz in_disk
    mov di,OFFSET msg                  ; "Installed NAME.WKX. Press Run ..."
    mov si,OFFSET t_done1
    call put_str
    mov si,OFFSET fname
    call put_str
    mov si,OFFSET t_done2
    call put_str
    xor al,al
    stosb
    jmp draw_status
in_disk:
    mov status,OFFSET t_disk
    jmp draw_status
in_error:
    call set_error
    jmp draw_status

; Leave the Store and start the selected program. That only works when it
; is on the disk, so this comes back when it has not been installed.
run_it:
    mov ax,sel
    cmp ax,count
    jb ru_go
    ret
ru_go:
    mov bx,REC_SIZE
    mul bx
    add ax,OFFSET heap
    mov recp,ax
    mov si,ax
    mov di,OFFSET fname
    call put_str
    mov si,OFFSET t_ext
    call put_str
    xor al,al
    stosb
    mov dx,OFFSET fname
    mov ah,5                           ; exit and run the program at DS:DX
    int 80h
    mov di,OFFSET msg                  ; still here: there is no such file
    mov si,recp
    call put_str
    mov si,OFFSET t_notyet
    call put_str
    xor al,al
    stosb
    mov status,OFFSET msg
    jmp draw_status

; ---- data ----

port    dw 0                           ; I/O address of the serial port
count   dw 0                           ; programs in the catalogue
pagetop dw 0                           ; first program on the page
sel     dw 0                           ; the selected program
row     dw 0
status  dw 0                           ; text for the status panel
recp    dw 0                           ; record being installed
rlen    dw 0
flen    dw 0
dots    db 0                           ; show progress while receiving
ink     db 0
fname   db 16 dup (0)
line    db 56 dup (0)
msg     db 56 dup (0)

errtab dw OFFSET t_silent,OFFSET t_refused,OFFSET t_large,OFFSET t_damaged

t_title   db 'WinKit Store',0
t_install db 'Install',0
t_run     db 'Run',0
t_reload  db 'Reload',0
t_prev    db '<',0
t_next    db '>',0
t_page    db 'Page ',0
t_ext     db '.WKX',0
t_dots    db '...',0
t_asking  db 'Contacting the store...',0
t_hint    db 'Select a program, then Install and Run.',0
t_empty   db 'The store is empty.',0
t_getting db 'Downloading ',0
t_done1   db 'Installed ',0
t_done2   db '. Press Run to start it.',0
t_notyet  db ' is not installed yet: press Install.',0
t_noport  db 'This machine has no serial port (COM1).',0
t_disk    db 'Could not save: disk error or disk full.',0
t_silent  db 'No answer. Start: python scripts/run.py --store',0
t_refused db 'The store does not have that program.',0
t_large   db 'The answer is too large for WinKit.',0
t_damaged db 'The download arrived damaged; try again.',0

; Not part of the file: the catalogue (CAT_MAX bytes), then the download.
heap:
CODE ENDS
END start
