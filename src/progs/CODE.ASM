; CODE.WKX - WinKit Code: a source editor with a built-in assembler, so that
; WinKit programs can be written, built and run inside WinKit.
;
; The editor keeps the text of one NAME.ASM file in memory (lines end in a
; single LF there; the file on disk has CR LF). Build assembles it to
; NAME.WKX with the assembler in the second half of this file; Run saves,
; builds and starts the program (INT 80h AH=5).
;
; The assembler takes the same MASM-style source as scripts/wkasm.py, minus
; MACRO and INCLUDE: labels, EQU, DB / DW (strings, DUP, ?), the 8086
; instructions WinKit programs use, [bx+si+n] and name[bx] operands, BYTE /
; WORD PTR, segment prefixes, OFFSET, and + - * / in expressions. SEGMENT,
; ASSUME, ORG, PROC, ENDP, ENDS and END lines are accepted and ignored. It
; always picks the longer of two encodings when a shorter one would depend
; on a value (16-bit displacements, near JMP), so its output is not byte for
; byte what wkasm gives, but it runs the same.
;
; Memory after the program image: the text, then a work area (the file
; while loading and saving, the program while assembling), then the names.
CODE SEGMENT PARA PUBLIC 'CODE'
ASSUME CS:CODE, DS:CODE
ORG 100h

K_BLACK   EQU 0                        ; the WinKit palette
K_NAVY    EQU 1
K_FACE    EQU 4
K_WHITE   EQU 6
BV_BUTTON EQU 0                        ; bevel styles
BV_FIELD  EQU 2
BV_PANEL  EQU 3

ROWS      EQU 12                       ; lines of text on screen
COLS      EQU 50                       ; characters across
TY        EQU 34                       ; top of the text, in the window

TEXT_MAX  EQU 12000                    ; characters of source
WORK_MAX  EQU 24100                    ; the file with CR LF; the program
OUT_MAX   EQU 16000                    ; largest program that can be built
SYM_MAX   EQU 300                      ; names
SYM_SIZE  EQU 16                       ; 12 characters, type, spare, value
W_OFS     EQU TEXT_MAX+2               ; work area, counted from heap
S_OFS     EQU W_OFS+WORK_MAX           ; names, counted from heap

O_KIND    EQU 0                        ; an operand, as parse_op leaves it
O_REG     EQU 1
O_SIZE    EQU 2                        ; 1 byte, 2 word, 0 unknown
O_RM      EQU 3
O_DISP    EQU 4                        ; has a displacement
O_BASE    EQU 5                        ; base registers: BX 1, BP 2, SI 4, DI 8
O_VAL     EQU 6
K_R8      EQU 1                        ; operand kinds
K_R16     EQU 2
K_SREG    EQU 3
K_IMM     EQU 4
K_MEM     EQU 5
C_OPEN    EQU 5bh                      ; the characters [ and ]
C_CLOSE   EQU 5dh

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

; Stop assembling with the message msg.
ERR MACRO msg
    mov errmsg,OFFSET msg
    jmp asm_error
ENDM

; Go to target when the word just read is kw.
IFTOK MACRO kw,target
    LOCAL skip
    mov di,OFFSET kw
    call tok_eq
    jne skip
    jmp target
skip:
ENDM

start:
    call new_text
    call draw_all
    mov dx,OFFSET cfgname              ; the file used last time, if any
    mov di,OFFSET basename
    mov cx,8
    mov ah,6
    int 80h
    cmp ax,0ffffh
    je events
    or ax,ax
    jz events
    mov bx,ax
    mov basename[bx],0
    call set_names
    call load
    call draw_name
    call show
events:
    mov ah,14h                         ; wait for a key or a click
    int 80h
    cmp ax,1
    jne ev_mouse
    jmp on_key
ev_mouse:
    cmp ax,2
    jne ev_close
    jmp on_click
ev_close:
    cmp ax,3
    jne events
    jmp want_quit

; ---- keyboard: BL = ASCII, BH = scan code ----
on_key:
    cmp bl,27
    jne ok_keep
    jmp want_quit
ok_keep:
    mov quitarmed,0
    mov al,bl
    cmp al,13
    jne ok_bs
    mov al,10                          ; Enter: a new line
    call ins_char
    mov full,1
    jmp shown
ok_bs:
    cmp al,8
    jne ok_tab
    mov bx,cur
    cmp bx,OFFSET heap
    je to_events
    dec bx
    mov cur,bx
    call del_at
    mov full,1
    jmp shown
ok_tab:
    cmp al,9
    jne ok_char
ok_tab1:
    mov al,' '                         ; Tab: spaces up to the next stop
    call ins_char
    mov bx,cur
    call line_start
    mov ax,cur
    sub ax,bx
    test al,3
    jnz ok_tab1
    jmp shown
ok_char:
    cmp al,32
    jb ok_special
    cmp al,126
    ja ok_special
    call ins_char
    jmp shown
to_events:
    jmp events
ok_special:
    mov al,bh
    cmp al,3ch                         ; F2
    jne ok_f3
    call do_save
    jmp events
ok_f3:
    cmp al,3dh                         ; F3
    jne ok_f5
    call do_open
    jmp events
ok_f5:
    cmp al,3fh                         ; F5
    jne ok_f9
    call do_build
    jmp events
ok_f9:
    cmp al,43h                         ; F9
    jne ok_move
    call do_run
    jmp events
ok_move:
    cmp al,4bh                         ; left
    jne ok_right
    mov bx,cur
    cmp bx,OFFSET heap
    je to_events
    dec cur
    jmp shown
ok_right:
    cmp al,4dh
    jne ok_up
    mov bx,cur
    cmp bx,tend
    jae to_events
    inc cur
    jmp shown
ok_up:
    cmp al,48h
    jne ok_down
    call go_up
    jmp shown
ok_down:
    cmp al,50h
    jne ok_home
    call go_down
    jmp shown
ok_home:
    cmp al,47h
    jne ok_end
    mov bx,cur
    call line_start
    mov cur,bx
    jmp shown
ok_end:
    cmp al,4fh
    jne ok_pgup
    mov bx,cur
    call line_end
    mov cur,bx
    jmp shown
ok_pgup:
    cmp al,49h
    jne ok_pgdn
    mov cx,ROWS-1
ok_pu:
    push cx
    call go_up
    pop cx
    loop ok_pu
    jmp shown
ok_pgdn:
    cmp al,51h
    jne ok_del
    mov cx,ROWS-1
ok_pd:
    push cx
    call go_down
    pop cx
    loop ok_pd
    jmp shown
ok_del:
    cmp al,53h
    jne to_events2
    mov bx,cur
    call del_at
    mov full,1
shown:
    call show
to_events2:
    jmp events

; ---- mouse: BX,CX = x,y in the client area ----
on_click:
    mov quitarmed,0
    cmp cx,3                           ; the toolbar
    jb to_events2
    cmp cx,25
    jae oc_text
    cmp bx,5
    jb to_events2
    cmp bx,53
    jae oc_open
    call new_text
    mov status,OFFSET t_new
    call draw_all
    jmp events
oc_open:
    cmp bx,57
    jb to_events2
    cmp bx,113
    jae oc_save
    call do_open
    jmp events
oc_save:
    cmp bx,117
    jb to_events2
    cmp bx,173
    jae oc_build
    call do_save
    jmp events
oc_build:
    cmp bx,177
    jb to_events2
    cmp bx,241
    jae oc_run
    call do_build
    jmp events
oc_run:
    cmp bx,245
    jb to_events3
    cmp bx,293
    jae to_events3
    call do_run
to_events3:
    jmp events
oc_text:
    sub cx,TY                          ; a click in the text: put the cursor
    jb to_events3
    mov ax,cx
    mov cl,16
    div cl
    xor ah,ah
    cmp ax,ROWS
    jae to_events3
    push bx
    call row_ptr                       ; BX -> that line
    pop ax
    jc to_events3
    sub ax,5
    jae oc_col
    xor ax,ax
oc_col:
    mov cl,3
    shr ax,cl
    add ax,left                        ; the column that was clicked
    mov dx,bx
    call line_end
    sub bx,dx                          ; length of the line
    cmp ax,bx
    jbe oc_set
    mov ax,bx
oc_set:
    add ax,dx
    mov cur,ax
    jmp shown

; Esc or the close button: leave, but not by accident with unsaved text.
want_quit:
    cmp modified,0
    je quit
    cmp quitarmed,0
    jne quit
    mov quitarmed,1
    mov dx,OFFSET t_unsaved
    call say
    jmp events
quit:
    xor ah,ah                          ; exit
    int 80h

; ---- the text ----

; BX = a place in the text -> BX = the start of its line.
line_start:
    cmp bx,OFFSET heap
    jbe ls_done
    cmp byte ptr [bx-1],10
    je ls_done
    dec bx
    jmp line_start
ls_done:
    ret

; BX = a place in the text -> BX = the end of its line (its LF, or tend).
line_end:
    cmp bx,tend
    jae le_done
    cmp byte ptr [bx],10
    je le_done
    inc bx
    jmp line_end
le_done:
    ret

; AX = a row of the screen -> BX = the line shown there. CF set: none.
row_ptr:
    mov cx,ax
    mov bx,top
rp_next:
    jcxz rp_ok
    call line_end
    cmp bx,tend
    jae rp_none
    inc bx
    dec cx
    jmp rp_next
rp_ok:
    clc
    ret
rp_none:
    stc
    ret

; Put AL in the text at the cursor.
ins_char:
    mov bx,tend
    cmp bx,OFFSET heap+TEXT_MAX
    jb ic_room
    mov dx,OFFSET t_full
    jmp say
ic_room:
    push ax
    mov cx,tend
    sub cx,cur
    mov si,tend
    mov di,si
    dec si
    std
    rep movsb                          ; make room, from the end back
    cld
    pop ax
    mov bx,cur
    mov [bx],al
    inc cur
    inc tend
    mov modified,1
    ret

; Take out the character at BX.
del_at:
    cmp bx,tend
    jae da_done
    mov di,bx
    mov si,bx
    inc si
    mov cx,tend
    sub cx,si
    rep movsb
    dec tend
    mov modified,1
da_done:
    ret

; Up and down keep the column where the lines are long enough.
go_up:
    mov bx,cur
    call line_start
    cmp bx,OFFSET heap
    je gu_done
    mov ax,cur
    sub ax,bx                          ; AX = column
    dec bx                             ; the LF before this line
    mov dx,bx
    call line_start
    sub dx,bx                          ; DX = length of the line above
    cmp ax,dx
    jbe gu_set
    mov ax,dx
gu_set:
    add ax,bx
    mov cur,ax
gu_done:
    ret

go_down:
    mov bx,cur
    call line_start
    mov ax,cur
    sub ax,bx                          ; AX = column
    mov bx,cur
    call line_end
    cmp bx,tend
    jae gd_done
    inc bx                             ; the start of the line below
    mov dx,bx
    call line_end
    sub bx,dx                          ; BX = its length
    cmp ax,bx
    jbe gd_set
    mov ax,bx
gd_set:
    add ax,dx
    mov cur,ax
gd_done:
    ret

; An empty editor holding the starting program.
new_text:
    mov si,OFFSET template
    mov di,OFFSET heap
nt_copy:
    lodsb
    or al,al
    jz nt_done
    stosb
    jmp nt_copy
nt_done:
    mov tend,di
    mov basename,0
    mov named,0
    mov srcname,0
    mov modified,0
; The cursor back to the top.
home_view:
    mov ax,OFFSET heap
    mov cur,ax
    mov top,ax
    mov left,0
    mov full,1
    ret

; ---- drawing ----

draw_all:
    mov dx,OFFSET t_title
    mov ah,10h                         ; a fresh, empty window
    int 80h
    GBEV 5,3,48,22,BV_BUTTON
    GTXT 17,6,K_BLACK,t_bnew
    GBEV 57,3,56,22,BV_BUTTON
    GTXT 69,6,K_BLACK,t_bopen
    GBEV 117,3,56,22,BV_BUTTON
    GTXT 129,6,K_BLACK,t_bsave
    GBEV 177,3,64,22,BV_BUTTON
    GTXT 189,6,K_BLACK,t_bbuild
    GBEV 245,3,48,22,BV_BUTTON
    GTXT 257,6,K_BLACK,t_brun
    GBEV 3,30,404,200,BV_FIELD
    call draw_name
    mov full,1
    call show
    mov dx,status
    jmp say

; The file name, right of the buttons.
draw_name:
    GBOX 300,3,108,22,K_FACE
    mov dx,OFFSET srcname
    cmp srcname,0
    jne dn_show
    mov dx,OFFSET t_noname
dn_show:
    mov bx,304
    mov cx,6
    mov al,K_BLACK
    mov ah,12h
    int 80h
    ret

; Show DX in the status panel, and remember it.
say:
    mov status,dx
    GBEV 3,238,404,20,BV_PANEL
    mov bx,9
    mov cx,240
    mov dx,status
    mov al,K_BLACK
    mov ah,12h
    int 80h
    ret

; Bring the screen up to date after the text or the cursor changed.
show:
    call fix_view
    cmp full,0
    je sh_two
    xor ax,ax
sh_all:
    push ax
    call draw_row
    pop ax
    inc ax
    cmp ax,ROWS
    jb sh_all
    jmp sh_cursor
sh_two:
    mov ax,oldrow                      ; only where the cursor was and is
    call draw_row
    mov ax,currow
    cmp ax,oldrow
    je sh_cursor
    call draw_row
sh_cursor:
    mov ax,currow
    mov oldrow,ax
    mov cx,16
    mul cx
    add ax,TY
    mov cx,ax
    mov bx,curcol
    sub bx,left
    shl bx,1
    shl bx,1
    shl bx,1
    add bx,5
    mov si,2
    mov di,16
    mov al,K_NAVY
    mov ah,11h
    int 80h
    mov full,0
    ret

; Scroll so that the cursor is on screen. Sets currow and curcol, and full
; when the view moved.
fix_view:
    mov bx,cur
    call line_start
    mov curline,bx
    cmp bx,top
    jae fv_count
    mov top,bx
    mov full,1
fv_count:
    xor dx,dx                          ; DX = rows from the top to the cursor
    mov bx,top
fv_walk:
    cmp bx,curline
    jae fv_got
    call line_end
    inc bx
    inc dx
    jmp fv_walk
fv_got:
    cmp dx,ROWS
    jb fv_row
    mov bx,top                         ; too far down: scroll a line, recount
    call line_end
    inc bx
    mov top,bx
    mov full,1
    jmp fv_count
fv_row:
    mov currow,dx
    mov ax,cur
    sub ax,curline
    mov curcol,ax
    cmp ax,left
    jae fv_right
    and ax,0fff8h
    mov left,ax
    mov full,1
    ret
fv_right:
    mov bx,left
    add bx,COLS
    cmp ax,bx
    jb fv_done
    sub ax,COLS-1
    mov left,ax
    mov full,1
fv_done:
    ret

; Draw screen row AX.
draw_row:
    push ax
    mov cx,16
    mul cx
    add ax,TY
    mov rowy,ax
    GBOX 5,rowy,400,16,K_WHITE
    pop ax
    call row_ptr
    jc dr_done
    mov cx,left                        ; skip what is scrolled off to the left
dr_skip:
    jcxz dr_copy
    cmp bx,tend
    jae dr_done
    cmp byte ptr [bx],10
    je dr_done
    inc bx
    dec cx
    jmp dr_skip
dr_copy:
    mov di,OFFSET line
    mov cx,COLS
dr_char:
    cmp bx,tend
    jae dr_end
    mov al,[bx]
    cmp al,10
    je dr_end
    cmp al,32
    jae dr_put
    mov al,' '
dr_put:
    stosb
    inc bx
    loop dr_char
dr_end:
    xor al,al
    stosb
    mov bx,5
    mov cx,rowy
    mov dx,OFFSET line
    mov al,K_BLACK
    mov ah,12h
    int 80h
dr_done:
    ret

; ---- files ----

; basename -> srcname "NAME.ASM" and outname "NAME.WKX".
set_names:
    mov di,OFFSET srcname
    mov si,OFFSET basename
    call put_str
    mov si,OFFSET t_asm
    call put_str
    xor al,al
    stosb
    mov di,OFFSET outname
    mov si,OFFSET basename
    call put_str
    mov si,OFFSET t_wkx
    call put_str
    xor al,al
    stosb
    mov named,1
    ret

; Ask for a name in the status panel, with the prompt at SI. It goes to
; basename. CF set when Esc was pressed or nothing was typed.
ask_name:
    mov askp,si
    mov asklen,0
an_show:
    mov di,OFFSET msg
    mov si,askp
    call put_str
    mov si,OFFSET askbuf
    mov cx,asklen
    rep movsb
    mov al,'_'
    stosb
    xor al,al
    stosb
    mov dx,OFFSET msg
    call say
an_key:
    mov ah,14h
    int 80h
    cmp ax,1
    jne an_key
    mov al,bl
    cmp al,27
    je an_cancel
    cmp al,13
    je an_done
    cmp al,8
    jne an_char
    cmp asklen,0
    je an_key
    dec asklen
    jmp an_show
an_char:
    call is_idch                       ; letters, digits and _ only
    jnc an_key
    cmp al,'a'
    jb an_add
    cmp al,'z'
    ja an_add
    sub al,20h
an_add:
    mov bx,asklen
    cmp bx,8
    jae an_key
    mov askbuf[bx],al
    inc asklen
    jmp an_show
an_done:
    cmp asklen,0
    je an_cancel
    mov si,OFFSET askbuf
    mov di,OFFSET basename
    mov cx,asklen
    rep movsb
    xor al,al
    stosb
    clc
    ret
an_cancel:
    mov dx,OFFSET t_cancel
    call say
    stc
    ret

do_open:
    mov si,OFFSET t_askopen
    call ask_name
    jc do_ret
    call set_names
    call load
    call draw_name
    jmp show
do_ret:
    ret

; Read srcname into the editor.
load:
    mov dx,OFFSET srcname
    mov di,OFFSET heap+W_OFS
    mov cx,WORK_MAX
    mov ah,6                           ; read a file
    int 80h
    cmp ax,0ffffh
    jne ld_have
    mov tend,OFFSET heap               ; a new, empty file of that name
    call home_view
    mov modified,0
    mov dx,OFFSET t_newfile
    jmp say
ld_have:
    mov cx,ax
    mov si,OFFSET heap+W_OFS
    mov di,OFFSET heap
    jcxz ld_end
ld_char:
    lodsb
    cmp al,13                          ; lines end in LF in memory
    je ld_next
    cmp al,26                          ; an old end-of-file mark
    je ld_end
    cmp al,9
    jne ld_keep
    mov al,' '
ld_keep:
    cmp di,OFFSET heap+TEXT_MAX
    jae ld_end
    stosb
ld_next:
    loop ld_char
ld_end:
    mov tend,di
    call home_view
    mov modified,0
    mov di,OFFSET msg
    mov si,OFFSET t_opened
    call put_str
    mov si,OFFSET srcname
    call put_str
    xor al,al
    stosb
    mov dx,OFFSET msg
    jmp say

; F2: write the text as srcname. CF set when it could not be done.
do_save:
    cmp named,0
    jne sv_go
    mov si,OFFSET t_asksave
    call ask_name
    jnc sv_named
    ret                                ; CF still set
sv_named:
    call set_names
    call draw_name
sv_go:
    mov si,OFFSET heap
    mov di,OFFSET heap+W_OFS
sv_char:
    cmp si,tend
    jae sv_write
    lodsb
    cmp al,10
    jne sv_put
    mov al,13                          ; CR LF on disk
    stosb
    mov al,10
sv_put:
    stosb
    jmp sv_char
sv_write:
    mov cx,di
    sub cx,OFFSET heap+W_OFS
    mov dx,OFFSET srcname
    mov si,OFFSET heap+W_OFS
    mov ah,4                           ; save a file
    int 80h
    or ax,ax
    jz sv_ok
    mov dx,OFFSET t_diskerr
    call say
    stc
    ret
sv_ok:
    mov modified,0
    mov dx,OFFSET cfgname              ; remember it for next time
    mov si,OFFSET basename
    mov di,si
    xor cx,cx
sv_len:
    cmp byte ptr [di],0
    je sv_cfg
    inc di
    inc cx
    jmp sv_len
sv_cfg:
    mov ah,4
    int 80h
    mov di,OFFSET msg
    mov si,OFFSET t_saved
    call put_str
    mov si,OFFSET srcname
    call put_str
    xor al,al
    stosb
    mov dx,OFFSET msg
    call say
    clc
    ret

; F5: save the source, assemble it and write NAME.WKX. CF set on failure.
do_build:
    call do_save
    jnc bd_go
    ret
bd_go:
    mov bx,tend
    mov byte ptr [bx],0                ; the assembler stops at a NUL
    call assemble
    jnc bd_ok
    mov di,OFFSET msg                  ; "Line n: what is wrong"
    mov si,OFFSET t_line
    call put_str
    mov ax,errline
    call put_dec
    mov si,OFFSET t_colon
    call put_str
    mov si,errmsg
    call put_str
    xor al,al
    stosb
    mov bx,OFFSET heap                 ; and the cursor goes to that line
    mov cx,errline
bd_find:
    dec cx
    jz bd_there
    call line_end
    cmp bx,tend
    jae bd_there
    inc bx
    jmp bd_find
bd_there:
    mov cur,bx
    mov full,1
    call show
    mov dx,OFFSET msg
    call say
    stc
    ret
bd_ok:
    mov dx,OFFSET outname
    mov si,OFFSET heap+W_OFS
    mov cx,outlen
    mov ah,4
    int 80h
    or ax,ax
    jz bd_saved
    mov dx,OFFSET t_diskerr
    call say
    stc
    ret
bd_saved:
    mov di,OFFSET msg
    mov si,OFFSET t_built
    call put_str
    mov si,OFFSET outname
    call put_str
    mov si,OFFSET t_comma
    call put_str
    mov ax,outlen
    call put_dec
    mov si,OFFSET t_bytes
    call put_str
    xor al,al
    stosb
    mov dx,OFFSET msg
    call say
    clc
    ret

; F9: build, then leave the editor and start the program.
do_run:
    call do_build
    jc rn_ret
    mov dx,OFFSET outname
    mov ah,5                           ; exit and run it
    int 80h
    mov dx,OFFSET t_norun              ; still here: it could not be started
    call say
rn_ret:
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

; =====================================================================
; The assembler. SI walks the text; pc is the address being assembled.
; Three passes: the first learns the names, the second learns their final
; addresses (sizes can depend on whether a name is a byte or a word), the
; third writes the program to the work area.
; =====================================================================

; Assemble the text (NUL at its end). CF clear: outlen bytes are in the
; work area. CF set: errmsg and errline say what is wrong.
assemble:
    mov asm_sp,sp
    mov symcount,0
    mov pass,0
as_pass:
    mov pc,100h
    mov outp,OFFSET heap+W_OFS
    mov lineno,1
    mov si,OFFSET heap
as_line:
    call do_line
    or al,al
    jnz as_line
    inc pass
    cmp pass,3
    jb as_pass
    mov ax,pc
    sub ax,100h
    mov outlen,ax
    clc
    ret
asm_error:
    mov ax,lineno
    mov errline,ax
    mov sp,asm_sp
    stc
    ret

; One line. AL = 0 when the text has ended.
do_line:
dl_start:
    call at_end
    jne dl_word
    jmp dl_eol
dl_word:
    call get_ident
    or cx,cx
    jnz dl_have
    ERR e_instr
dl_have:
    cmp byte ptr [si],':'
    jne dl_kw
    inc si                             ; "name:" is a label
    mov di,OFFSET tok
    xor al,al
    mov dx,pc
    call sym_define
    jmp dl_start
dl_kw:
    IFTOK k_assume,dl_skip
    IFTOK k_end,dl_skip
    IFTOK k_org,dl_skip
    IFTOK k_db,dl_db
    IFTOK k_dw,dl_dw
    mov si_save,si                     ; is the second word a directive?
    push si
    mov si,OFFSET tok
    mov di,OFFSET tok1
    mov cx,16
    rep movsb
    pop si
    call skip_sp
    call get_ident
    IFTOK k_equ,dl_equ
    IFTOK k_db,dl_ndb
    IFTOK k_dw,dl_ndw
    IFTOK k_segment,dl_skip
    IFTOK k_ends,dl_skip
    IFTOK k_endp,dl_skip
    IFTOK k_proc,dl_proc
    mov si,OFFSET tok1                 ; no: the first word is an instruction
    mov di,OFFSET tok
    mov cx,16
    rep movsb
    mov si,si_save
    jmp dl_mnem
dl_equ:
    call eval_expr
    mov dx,ax
    mov al,3
    mov di,OFFSET tok1
    call sym_define
    jmp dl_eol
dl_ndb:
    mov di,OFFSET tok1
    mov al,1
    mov dx,pc
    call sym_define
dl_db:
    call data_b
    jmp dl_eol
dl_ndw:
    mov di,OFFSET tok1
    mov al,2
    mov dx,pc
    call sym_define
dl_dw:
    call data_w
    jmp dl_eol
dl_proc:
    mov di,OFFSET tok1
    xor al,al
    mov dx,pc
    call sym_define
    jmp dl_skip
dl_again:                              ; after a prefix such as REP
    call skip_sp
    call get_ident
dl_mnem:
    mov bx,OFFSET mnemtab
dm_next:
    cmp byte ptr [bx],0
    jne dm_cmp
    ERR e_instr
dm_cmp:
    mov di,bx
    call tok_eq
    pushf
dm_skip:
    cmp byte ptr [bx],0
    je dm_end
    inc bx
    jmp dm_skip
dm_end:
    inc bx                             ; BX -> class, argument
    popf
    je dm_found
    add bx,2
    jmp dm_next
dm_found:
    mov al,[bx+1]
    mov harg,al
    mov bl,[bx]
    xor bh,bh
    shl bx,1
    mov ax,OFFSET dl_eol               ; the handler returns to dl_eol
    push ax
    mov ax,htab[bx]
    jmp ax
dl_skip:
    mov al,[si]                        ; nothing on this line matters
    or al,al
    jz dl_eol
    cmp al,10
    je dl_eol
    inc si
    jmp dl_skip
dl_eol:
    call at_end
    je dl_fine
    ERR e_extra
dl_fine:
    mov al,[si]
    cmp al,';'
    jne dl_nl
dl_cmt:
    inc si
    mov al,[si]
    or al,al
    jz dl_nl
    cmp al,10
    jne dl_cmt
dl_nl:
    or al,al
    jz dl_ret
    inc si
    inc lineno
    mov al,1
dl_ret:
    ret

; ---- reading the text ----

skip_sp:
    cmp byte ptr [si],' '
    je ss_adv
    cmp byte ptr [si],9
    jne ss_ret
ss_adv:
    inc si
    jmp skip_sp
ss_ret:
    ret

; ZF set when nothing but a comment is left on the line.
at_end:
    call skip_sp
    mov al,[si]
    or al,al
    jz ae_ret
    cmp al,10
    je ae_ret
    cmp al,';'
ae_ret:
    ret

; CF set when AL can start a name: a letter, _ ? @ or $.
is_alpha:
    cmp al,'_'
    je ia_yes
    cmp al,'?'
    je ia_yes
    cmp al,'@'
    je ia_yes
    cmp al,'$'
    je ia_yes
    push ax
    and al,0dfh
    cmp al,'A'
    jb ia_no
    cmp al,'Z'
    ja ia_no
    pop ax
ia_yes:
    stc
    ret
ia_no:
    pop ax
    clc
    ret

; CF set when AL can be inside a name: those, or a digit.
is_idch:
    cmp al,'0'
    jb is_alpha
    cmp al,'9'
    ja is_alpha
    stc
    ret

; Read a name at SI into tok: upper case, padded with zeros to 16 bytes.
; CX = its length, 0 when there is no name here.
get_ident:
    push ax
    push di
    mov di,OFFSET tok
    xor ax,ax
    mov cx,8
    rep stosw
    mov di,OFFSET tok
    xor cx,cx
    mov al,[si]
    call is_alpha
    jnc gi_done
gi_next:
    mov al,[si]
    call is_idch
    jnc gi_done
    cmp al,'a'
    jb gi_store
    cmp al,'z'
    ja gi_store
    sub al,20h
gi_store:
    inc si
    cmp cx,15
    jae gi_next
    mov [di],al
    inc di
    inc cx
    jmp gi_next
gi_done:
    pop di
    pop ax
    ret

; ZF set when tok is the keyword at DI.
tok_eq:
    push ax
    push si
    mov si,OFFSET tok
te_next:
    mov al,[di]
    cmp al,[si]
    jne te_out
    inc si
    inc di
    or al,al
    jnz te_next
te_out:
    pop si
    pop ax
    ret

; A number at SI (decimal, or hexadecimal with an h after it) -> AX.
get_num:
    push bx
    push cx
    push dx
    push di
    mov di,si
gn_scan:
    mov al,[di]
    call is_idch
    jnc gn_end
    inc di
    jmp gn_scan
gn_end:
    mov bx,10
    mov al,[di-1]
    and al,0dfh
    cmp al,'H'
    jne gn_conv
    mov bx,16
    dec di
gn_conv:
    xor ax,ax
gn_digit:
    cmp si,di
    jae gn_fin
    mov cl,[si]
    inc si
    cmp cl,'9'
    jbe gn_09
    and cl,0dfh
    sub cl,'A'-10
    jmp gn_have
gn_09:
    sub cl,'0'
gn_have:
    xor ch,ch
    cmp cx,bx
    jae gn_bad
    mul bx
    add ax,cx
    jmp gn_digit
gn_fin:
    cmp bx,16
    jne gn_out
    inc si                             ; the h
gn_out:
    pop di
    pop dx
    pop cx
    pop bx
    ret
gn_bad:
    ERR e_number

; ---- names ----

; DI -> a 16-byte name. BX -> its entry; CF set when there is none (BX is
; then where a new entry goes).
sym_find:
    push ax
    push cx
    push dx
    push si
    mov dx,symcount
    mov bx,OFFSET heap+S_OFS
sf_next:
    or dx,dx
    jz sf_none
    push di
    mov si,bx
    mov cx,12
    repe cmpsb
    pop di
    je sf_hit
    add bx,SYM_SIZE
    dec dx
    jmp sf_next
sf_none:
    stc
    jmp sf_out
sf_hit:
    clc
sf_out:
    pop si
    pop dx
    pop cx
    pop ax
    ret

; Give the name at DI the type AL (0 label, 1 byte, 2 word, 3 number) and
; the value DX.
sym_define:
    push bx
    call sym_find
    jc sd_new
    cmp pass,0
    jne sd_known
    ERR e_twice
sd_known:
    cmp pass,2
    jne sd_store
    cmp [bx+14],dx
    je sd_store
    ERR e_phase
sd_store:
    mov [bx+12],al
    mov [bx+14],dx
    pop bx
    ret
sd_new:
    cmp symcount,SYM_MAX
    jb sd_add
    ERR e_names
sd_add:
    push cx
    push si
    push di
    mov si,di
    mov di,bx
    mov cx,12
    rep movsb
    pop di
    pop si
    pop cx
    mov [bx+12],al
    mov [bx+14],dx
    inc symcount
    pop bx
    ret

; ---- values ----

; An expression at SI -> AX. ex_mem is set when it names a place in memory
; (a label or variable without OFFSET); ex_size is then 1, 2 or 0.
eval_expr:
    mov ex_mem,0
    mov ex_size,0
eval_more:
    call eval_term
ee_loop:
    call skip_sp
    mov bl,[si]
    cmp bl,'+'
    jne ee_minus
    inc si
    push ax
    call eval_term
    pop bx
    add ax,bx
    jmp ee_loop
ee_minus:
    cmp bl,'-'
    jne ee_times
    inc si
    push ax
    call eval_term
    mov bx,ax
    pop ax
    sub ax,bx
    jmp ee_loop
ee_times:
    cmp bl,'*'
    jne ee_over
    inc si
    push ax
    call eval_term
    pop bx
    mul bx
    jmp ee_loop
ee_over:
    cmp bl,'/'
    jne ee_done
    inc si
    push ax
    call eval_term
    mov bx,ax
    pop ax
    or bx,bx
    jnz ee_div
    ERR e_number
ee_div:
    xor dx,dx
    div bx
    jmp ee_loop
ee_done:
    ret

; One number, character, name or (expression) -> AX.
eval_term:
    call skip_sp
    mov al,[si]
    cmp al,'0'
    jb et_notnum
    cmp al,'9'
    ja et_notnum
    jmp get_num
et_notnum:
    cmp al,27h
    jne et_paren
    mov al,[si+1]                      ; 'c'
    cmp byte ptr [si+2],27h
    je et_chr
    ERR e_string
et_chr:
    add si,3
    xor ah,ah
    ret
et_paren:
    cmp al,'('
    jne et_neg
    inc si
    call eval_more
    call skip_sp
    cmp byte ptr [si],')'
    je et_closed
    ERR e_value
et_closed:
    inc si
    ret
et_neg:
    cmp al,'-'
    jne et_name
    inc si
    call eval_term
    neg ax
    ret
et_name:
    call is_alpha
    jc et_ident
    ERR e_value
et_ident:
    call get_ident
    mov di,OFFSET k_dollar
    call tok_eq
    jne et_notpc
    mov ax,pc
    ret
et_notpc:
    mov di,OFFSET k_offset
    call tok_eq
    jne et_sym
    call skip_sp
    call get_ident
    or cx,cx
    jnz et_lookup
    ERR e_value
et_sym:
    call et_lookup
    cmp bl,3                           ; a number made with EQU
    je et_ret
    mov ex_mem,1
    cmp bl,0ffh
    jne et_typed
    xor bl,bl
et_typed:
    mov ex_size,bl
et_ret:
    ret
; The value of the name in tok -> AX, its type -> BL (FFh: not known yet).
et_lookup:
    mov di,OFFSET tok
    call sym_find
    jc el_none
    mov ax,[bx+14]
    mov bl,[bx+12]
    ret
el_none:
    cmp pass,2
    jne el_later
    ERR e_name
el_later:
    xor ax,ax
    mov bl,0ffh
    ret

; The word in tok (CX letters) as a register: CF clear, AL = kind, AH = its
; number. CF set when it is not one.
reg_lookup:
    cmp cx,2
    jne rl_no
    mov ax,word ptr tok
    mov bx,OFFSET regtab
    xor dx,dx
rl_next:
    cmp ax,[bx]
    je rl_hit
    add bx,2
    inc dx
    cmp dx,20
    jb rl_next
rl_no:
    stc
    ret
rl_hit:
    mov ah,dl
    mov al,K_R8
    cmp ah,8
    jb rl_ok
    sub ah,8
    mov al,K_R16
    cmp ah,8
    jb rl_ok
    sub ah,8
    mov al,K_SREG
rl_ok:
    clc
    ret

; ---- operands ----

; Read one operand at SI into the 8 bytes at DI.
parse_op:
    mov bp,di
    push di
    xor ax,ax
    mov cx,4
    rep stosw
    pop di
po_start:
    call skip_sp
    cmp byte ptr [si],C_OPEN
    jne po_word
    jmp po_bracket
po_word:
    mov si_op,si
    call get_ident
    or cx,cx
    jnz po_named
    jmp po_expr
po_named:
    mov di,OFFSET k_byte
    call tok_eq
    jne po_notbyte
    mov byte ptr [bp+O_SIZE],1
    jmp po_ptr
po_notbyte:
    mov di,OFFSET k_word
    call tok_eq
    jne po_notword
    mov byte ptr [bp+O_SIZE],2
po_ptr:
    call skip_sp                       ; BYTE and WORD are followed by PTR
    call get_ident
    mov di,OFFSET k_ptr
    call tok_eq
    je po_start
    ERR e_ops
po_notword:
    call reg_lookup
    jc po_notreg
    cmp al,K_SREG
    jne po_reg
    cmp byte ptr [si],':'
    jne po_reg
    inc si                             ; "es:" is a prefix byte, written now
    mov al,ah
    shl al,1
    shl al,1
    shl al,1
    or al,26h
    call emit
    jmp po_start
po_reg:
    mov [bp+O_KIND],al
    mov [bp+O_REG],ah
    mov byte ptr [bp+O_SIZE],2
    cmp al,K_R8
    jne po_regdone
    mov byte ptr [bp+O_SIZE],1
po_regdone:
    ret
po_notreg:
    mov si,si_op                       ; a name: let the expression read it
po_expr:
    call eval_expr
    mov [bp+O_VAL],ax
    cmp ex_mem,0
    jne po_mem
    call skip_sp
    cmp byte ptr [si],C_OPEN
    je po_disp
    mov byte ptr [bp+O_KIND],K_IMM
    ret
po_mem:
    cmp byte ptr [bp+O_SIZE],0
    jne po_disp
    mov al,ex_size
    mov [bp+O_SIZE],al
po_disp:
    mov byte ptr [bp+O_DISP],1
    call skip_sp
    cmp byte ptr [si],C_OPEN
    jne po_memdone
po_bracket:
    inc si
po_bterm:
    call skip_sp
    mov si_op,si
    call get_ident
    or cx,cx
    jz po_bexpr
    call reg_lookup
    jc po_bname
    cmp al,K_R16
    je po_base
    ERR e_addr
po_base:
    mov bl,ah
    xor bh,bh
    mov al,basebit[bx]
    or al,al
    jnz po_baseok
    ERR e_addr
po_baseok:
    or [bp+O_BASE],al
    jmp po_bafter
po_bname:
    mov si,si_op
po_bexpr:
    call eval_term
    add [bp+O_VAL],ax
    mov byte ptr [bp+O_DISP],1
po_bafter:
    call skip_sp
    mov al,[si]
    cmp al,'+'
    jne po_bminus
    inc si
    jmp po_bterm
po_bminus:
    cmp al,'-'
    jne po_bclose
    inc si
    call eval_term
    sub [bp+O_VAL],ax
    mov byte ptr [bp+O_DISP],1
    jmp po_bafter
po_bclose:
    cmp al,C_CLOSE
    je po_bend
    ERR e_addr
po_bend:
    inc si
    call skip_sp
    cmp byte ptr [si],C_OPEN
    je po_bracket
po_memdone:
    mov byte ptr [bp+O_KIND],K_MEM
    mov bl,[bp+O_BASE]
    xor bh,bh
    mov al,rmtab[bx]
    cmp al,0ffh
    jne po_rmok
    ERR e_addr
po_rmok:
    mov [bp+O_RM],al
    cmp bl,2                           ; [bp] alone needs a displacement
    jne po_ret
    mov byte ptr [bp+O_DISP],1
po_ret:
    ret

; The two operands of an instruction, into op1 and op2.
two_ops:
    mov di,OFFSET op1
    call parse_op
    call skip_sp
    cmp byte ptr [si],','
    je to_comma
    ERR e_ops
to_comma:
    inc si
    mov di,OFFSET op2
    jmp parse_op

; The one operand of an instruction, into op1; op2 is cleared.
one_op:
    mov op2,0
    mov di,OFFSET op1
    jmp parse_op

; Byte or word? AL = 0 or 1, decided by the registers, then by memory.
get_w:
    mov bx,OFFSET op1
    cmp byte ptr [bx+O_KIND],K_IMM
    jne gw_first
    ERR e_ops
gw_first:
    mov al,[bx+O_SIZE]
    cmp byte ptr [bx+O_KIND],K_MEM
    jne gw_reg1
    mov bx,OFFSET op2                  ; op1 is memory: a register decides
    cmp byte ptr [bx+O_KIND],K_R8
    je gw_reg2
    cmp byte ptr [bx+O_KIND],K_R16
    jne gw_check
gw_reg2:
    mov al,[bx+O_SIZE]
    jmp gw_check
gw_reg1:
    mov bx,OFFSET op2                  ; both registers: the same size
    cmp byte ptr [bx+O_KIND],K_R8
    je gw_both
    cmp byte ptr [bx+O_KIND],K_R16
    jne gw_check
gw_both:
    cmp al,[bx+O_SIZE]
    je gw_check
    ERR e_sizes
gw_check:
    or al,al
    jnz gw_ok
    cmp pass,2
    jne gw_guess
    ERR e_size
gw_guess:
    mov al,2
gw_ok:
    dec al
    ret

; The same from op1 alone.
get_w1:
    mov bx,OFFSET op1
    mov al,[bx+O_SIZE]
    jmp gw_check

; ---- writing the program ----

; Write the byte AL.
emit:
    cmp pass,2
    jne em_count
    push bx
    mov bx,outp
    cmp bx,OFFSET heap+W_OFS+OUT_MAX
    jb em_room
    ERR e_big
em_room:
    mov [bx],al
    inc outp
    pop bx
em_count:
    inc pc
    ret

; Write the word AX.
emit_word:
    call emit
    xchg al,ah
    call emit
    xchg al,ah
    ret

; Write the ModRM byte (and displacement) for the operand at DI, with AL
; in its register field.
emit_rm:
    shl al,1
    shl al,1
    shl al,1
    mov bl,al
    cmp byte ptr [di+O_KIND],K_MEM
    je er_mem
    mov al,[di+O_REG]
    or al,0c0h
    or al,bl
    jmp emit
er_mem:
    cmp byte ptr [di+O_BASE],0
    jne er_based
    mov al,bl                          ; a plain address
    or al,6
    call emit
    mov ax,[di+O_VAL]
    jmp emit_word
er_based:
    mov al,[di+O_RM]
    or al,bl
    cmp byte ptr [di+O_DISP],0
    je emit
    or al,80h
    call emit
    mov ax,[di+O_VAL]
    jmp emit_word

; Write op2's value as a byte (DL = 0) or a word (DL = 1).
emit_imm:
    mov bx,OFFSET op2
    mov ax,[bx+O_VAL]
    or dl,dl
    jz emit
    jmp emit_word

; ---- one routine per kind of instruction; harg comes from mnemtab ----

h_simple:
    mov al,harg
    jmp emit

h_prefix:
    mov al,harg
    call emit
    pop ax                             ; not back to dl_eol: read on
    jmp dl_again

h_mov:
    call two_ops
    mov bx,OFFSET op1
    cmp byte ptr [bx+O_KIND],K_SREG
    jne hm_from
    mov al,8eh                         ; segment register, r/m
    call emit
    mov al,[bx+O_REG]
    mov di,OFFSET op2
    jmp emit_rm
hm_from:
    mov bx,OFFSET op2
    cmp byte ptr [bx+O_KIND],K_SREG
    jne hm_plain
    mov al,8ch                         ; r/m, segment register
    call emit
    mov al,[bx+O_REG]
    mov di,OFFSET op1
    jmp emit_rm
hm_plain:
    call get_w
    mov dl,al
    mov bx,OFFSET op2
    cmp byte ptr [bx+O_KIND],K_IMM
    jne hm_reg
    mov bx,OFFSET op1
    cmp byte ptr [bx+O_KIND],K_MEM
    je hm_memimm
    mov al,dl                          ; register, number
    shl al,1
    shl al,1
    shl al,1
    add al,0b0h
    add al,[bx+O_REG]
    call emit
    jmp emit_imm
hm_memimm:
    mov al,0c6h
    add al,dl
    call emit
    xor al,al
    mov di,OFFSET op1
    call emit_rm
    jmp emit_imm
hm_reg:
    cmp byte ptr [bx+O_KIND],K_MEM
    je hm_load
    mov al,88h                         ; r/m, register
    add al,dl
    call emit
    mov bx,OFFSET op2
    mov al,[bx+O_REG]
    mov di,OFFSET op1
    jmp emit_rm
hm_load:
    mov al,8ah                         ; register, memory
    add al,dl
    call emit
    mov bx,OFFSET op1
    mov al,[bx+O_REG]
    mov di,OFFSET op2
    jmp emit_rm

; ADD OR ADC SBB AND SUB XOR CMP: harg is 0..7.
h_alu:
    call two_ops
    call get_w
    mov dl,al
    mov bx,OFFSET op2
    cmp byte ptr [bx+O_KIND],K_IMM
    jne ha_reg
    mov al,80h
    add al,dl
    call emit
    mov al,harg
    mov di,OFFSET op1
    call emit_rm
    jmp emit_imm
ha_reg:
    mov al,harg
    shl al,1
    shl al,1
    shl al,1
    add al,dl
    cmp byte ptr [bx+O_KIND],K_MEM
    je ha_load
    call emit                          ; r/m, register
    mov bx,OFFSET op2
    mov al,[bx+O_REG]
    mov di,OFFSET op1
    jmp emit_rm
ha_load:
    add al,2                           ; register, memory
    call emit
    mov bx,OFFSET op1
    mov al,[bx+O_REG]
    mov di,OFFSET op2
    jmp emit_rm

h_test:
    call two_ops
    call get_w
    mov dl,al
    mov bx,OFFSET op2
    cmp byte ptr [bx+O_KIND],K_IMM
    jne ht_reg
    mov al,0f6h
    add al,dl
    call emit
    xor al,al
    mov di,OFFSET op1
    call emit_rm
    jmp emit_imm
ht_reg:
    mov al,84h
    add al,dl
    call emit
    mov bx,OFFSET op2
    cmp byte ptr [bx+O_KIND],K_MEM
    je ht_load
    mov al,[bx+O_REG]
    mov di,OFFSET op1
    jmp emit_rm
ht_load:
    mov bx,OFFSET op1
    mov al,[bx+O_REG]
    mov di,OFFSET op2
    jmp emit_rm

; INC (harg 0) and DEC (1).
h_incdec:
    call one_op
    mov bx,OFFSET op1
    cmp byte ptr [bx+O_KIND],K_R16
    jne hi_rm
    mov al,harg
    shl al,1
    shl al,1
    shl al,1
    add al,40h
    add al,[bx+O_REG]
    jmp emit
hi_rm:
    call get_w1
    add al,0feh
    call emit
    mov al,harg
    mov di,OFFSET op1
    jmp emit_rm

; NOT NEG MUL IMUL DIV IDIV: harg is 2..7.
h_grp3:
    call one_op
    call get_w1
    add al,0f6h
    call emit
    mov al,harg
    mov di,OFFSET op1
    jmp emit_rm

; Shifts and rotates, by 1 or by CL.
h_shift:
    call two_ops
    call get_w1
    mov dl,al
    mov bx,OFFSET op2
    cmp byte ptr [bx+O_KIND],K_IMM
    jne hs_cl
    cmp word ptr [bx+O_VAL],1
    je hs_one
    ERR e_shift
hs_one:
    mov al,0d0h
    jmp hs_go
hs_cl:
    cmp byte ptr [bx+O_KIND],K_R8
    jne hs_bad
    cmp byte ptr [bx+O_REG],1
    je hs_bycl
hs_bad:
    ERR e_shift
hs_bycl:
    mov al,0d2h
hs_go:
    add al,dl
    call emit
    mov al,harg
    mov di,OFFSET op1
    jmp emit_rm

; PUSH (harg 0) and POP (1).
h_pushpop:
    call one_op
    mov bx,OFFSET op1
    mov al,[bx+O_KIND]
    cmp al,K_R16
    jne hp_seg
    mov al,harg
    shl al,1
    shl al,1
    shl al,1
    add al,50h
    add al,[bx+O_REG]
    jmp emit
hp_seg:
    cmp al,K_SREG
    jne hp_mem
    mov al,[bx+O_REG]
    shl al,1
    shl al,1
    shl al,1
    add al,6
    add al,harg
    jmp emit
hp_mem:
    cmp al,K_MEM
    je hp_rm
    ERR e_ops
hp_rm:
    cmp harg,0
    jne hp_pop
    mov al,0ffh
    call emit
    mov al,6
    mov di,OFFSET op1
    jmp emit_rm
hp_pop:
    mov al,8fh
    call emit
    xor al,al
    mov di,OFFSET op1
    jmp emit_rm

h_int:
    call eval_expr
    push ax
    mov al,0cdh
    call emit
    pop ax
    jmp emit

; Conditional jumps, LOOP and JCXZ: harg is the opcode, the target is near.
h_jcc:
    call eval_expr
    push ax
    mov al,harg
    call emit
    pop ax
    mov bx,pc
    inc bx
    sub ax,bx
    cmp pass,2
    jne hj_emit
    cmp ax,127
    jg hj_far
    cmp ax,-128
    jge hj_emit
hj_far:
    ERR e_far
hj_emit:
    jmp emit

; JMP: near unless SHORT is written.
h_jmp:
    call skip_sp
    mov si_op,si
    call get_ident
    mov di,OFFSET k_short
    call tok_eq
    jne hj_notshort
    mov harg,0ebh
    jmp h_jcc
hj_notshort:
    mov di,OFFSET k_near
    call tok_eq
    je hj_near
    mov si,si_op
hj_near:
    mov harg,0e9h
; CALL and near JMP: harg is the opcode.
h_call:
    call eval_expr
    push ax
    mov al,harg
    call emit
    pop ax
    sub ax,pc
    sub ax,2
    jmp emit_word

h_xchg:
    call two_ops
    call get_w
    add al,86h
    call emit
    mov bx,OFFSET op2
    cmp byte ptr [bx+O_KIND],K_MEM
    je hx_mem
    mov al,[bx+O_REG]
    mov di,OFFSET op1
    jmp emit_rm
hx_mem:
    mov bx,OFFSET op1
    mov al,[bx+O_REG]
    mov di,OFFSET op2
    jmp emit_rm

h_lea:
    call two_ops
    mov al,8dh
    call emit
    mov bx,OFFSET op1
    mov al,[bx+O_REG]
    mov di,OFFSET op2
    jmp emit_rm

; IN AL/AX, DX or a port number.
h_in:
    call two_ops
    mov bx,OFFSET op1
    mov dl,[bx+O_SIZE]
    dec dl
    mov bx,OFFSET op2
    cmp byte ptr [bx+O_KIND],K_IMM
    je hin_port
    mov al,0ech
    add al,dl
    jmp emit
hin_port:
    mov al,0e4h
    add al,dl
    call emit
    mov al,[bx+O_VAL]
    jmp emit

; OUT DX or a port number, AL/AX.
h_out:
    call two_ops
    mov bx,OFFSET op2
    mov dl,[bx+O_SIZE]
    dec dl
    mov bx,OFFSET op1
    cmp byte ptr [bx+O_KIND],K_IMM
    je hout_port
    mov al,0eeh
    add al,dl
    jmp emit
hout_port:
    mov al,0e6h
    add al,dl
    call emit
    mov al,[bx+O_VAL]
    jmp emit

; ---- DB and DW ----

data_b:
db_item:
    call skip_sp
    mov al,[si]
    cmp al,27h
    jne db_notstr
    inc si
db_chr:
    mov al,[si]                        ; a string: every character
    or al,al
    jz db_bad
    cmp al,10
    je db_bad
    inc si
    cmp al,27h
    jne db_put
    cmp byte ptr [si],27h              ; '' inside is one quote
    jne db_next
    inc si
db_put:
    call emit
    jmp db_chr
db_bad:
    ERR e_string
db_notstr:
    cmp al,'?'
    jne db_val
    inc si
    xor al,al
    call emit
    jmp db_next
db_val:
    call eval_expr
    call dup_check
    jcxz db_next
db_rep:
    call emit
    loop db_rep
db_next:
    call skip_sp
    cmp byte ptr [si],','
    jne db_done
    inc si
    jmp db_item
db_done:
    ret

data_w:
dw_item:
    call skip_sp
    cmp byte ptr [si],'?'
    jne dw_val
    inc si
    xor ax,ax
    call emit_word
    jmp dw_next
dw_val:
    call eval_expr
    call dup_check
    jcxz dw_next
dw_rep:
    call emit_word
    loop dw_rep
dw_next:
    call skip_sp
    cmp byte ptr [si],','
    jne dw_done
    inc si
    jmp dw_item
dw_done:
    ret

; After a value AX: is it "n DUP (v)"? CX = how many times, AX = what.
dup_check:
    push ax
    call skip_sp
    mov si_op,si
    call get_ident
    mov di,OFFSET k_dup
    call tok_eq
    je dc_dup
    mov si,si_op
    pop ax
    mov cx,1
    ret
dc_dup:
    pop cx
    call skip_sp
    cmp byte ptr [si],'('
    jne dc_bad
    inc si
    call skip_sp
    xor ax,ax
    cmp byte ptr [si],'?'
    jne dc_expr
    inc si
    jmp dc_close
dc_expr:
    push cx
    call eval_expr
    pop cx
dc_close:
    call skip_sp
    cmp byte ptr [si],')'
    jne dc_bad
    inc si
    ret
dc_bad:
    ERR e_value

; ---- data ----

tend     dw 0                          ; end of the text
cur      dw 0                          ; the cursor
top      dw 0                          ; first line on screen
left     dw 0                          ; first column on screen
curline  dw 0
currow   dw 0
curcol   dw 0
oldrow   dw 0
rowy     dw 0
status   dw OFFSET t_ready
askp     dw 0
asklen   dw 0
full     db 1                          ; redraw every row
modified db 0
named    db 0
quitarmed db 0
basename db 9 dup (0)                  ; NAME
srcname  db 14 dup (0)                 ; NAME.ASM
outname  db 14 dup (0)                 ; NAME.WKX
askbuf   db 9 dup (0)
line     db 56 dup (0)
msg      db 80 dup (0)
cfgname  db 'CODE.CFG',0

asm_sp   dw 0                          ; the assembler
pc       dw 0
outp     dw 0
outlen   dw 0
lineno   dw 0
errline  dw 0
errmsg   dw 0
symcount dw 0
si_save  dw 0
si_op    dw 0
pass     db 0
harg     db 0
ex_mem   db 0
ex_size  db 0
tok      db 16 dup (0)
tok1     db 16 dup (0)
op1      db 8 dup (0)
op2      db 8 dup (0)

regtab   db 'ALCLDLBLAHCHDHBHAXCXDXBXSPBPSIDIESCSSSDS'
basebit  db 0,0,0,1,0,2,4,8            ; by register: BX, BP, SI, DI
rmtab    db 6,7,6,0ffh,4,0,2,0ffh,5,1,3 ; by base registers

htab dw OFFSET h_simple,OFFSET h_prefix,OFFSET h_mov,OFFSET h_alu
     dw OFFSET h_test,OFFSET h_incdec,OFFSET h_grp3,OFFSET h_shift
     dw OFFSET h_pushpop,OFFSET h_int,OFFSET h_jcc,OFFSET h_jmp
     dw OFFSET h_call,OFFSET h_xchg,OFFSET h_lea,OFFSET h_in,OFFSET h_out

; Each instruction: its name, the routine (index into htab), an argument.
mnemtab db 'MOV',0,2,0
    db 'ADD',0,3,0, 'OR',0,3,1, 'ADC',0,3,2, 'SBB',0,3,3
    db 'AND',0,3,4, 'SUB',0,3,5, 'XOR',0,3,6, 'CMP',0,3,7
    db 'TEST',0,4,0
    db 'INC',0,5,0, 'DEC',0,5,1
    db 'NOT',0,6,2, 'NEG',0,6,3, 'MUL',0,6,4, 'IMUL',0,6,5
    db 'DIV',0,6,6, 'IDIV',0,6,7
    db 'ROL',0,7,0, 'ROR',0,7,1, 'RCL',0,7,2, 'RCR',0,7,3
    db 'SHL',0,7,4, 'SAL',0,7,4, 'SHR',0,7,5, 'SAR',0,7,7
    db 'PUSH',0,8,0, 'POP',0,8,1
    db 'INT',0,9,0
    db 'JMP',0,11,0, 'CALL',0,12,0e8h
    db 'JE',0,10,74h, 'JZ',0,10,74h, 'JNE',0,10,75h, 'JNZ',0,10,75h
    db 'JB',0,10,72h, 'JC',0,10,72h, 'JNAE',0,10,72h
    db 'JAE',0,10,73h, 'JNB',0,10,73h, 'JNC',0,10,73h
    db 'JBE',0,10,76h, 'JNA',0,10,76h, 'JA',0,10,77h, 'JNBE',0,10,77h
    db 'JL',0,10,7ch, 'JNGE',0,10,7ch, 'JGE',0,10,7dh, 'JNL',0,10,7dh
    db 'JLE',0,10,7eh, 'JNG',0,10,7eh, 'JG',0,10,7fh, 'JNLE',0,10,7fh
    db 'JS',0,10,78h, 'JNS',0,10,79h, 'JO',0,10,70h, 'JNO',0,10,71h
    db 'JP',0,10,7ah, 'JPE',0,10,7ah, 'JNP',0,10,7bh, 'JPO',0,10,7bh
    db 'LOOP',0,10,0e2h, 'LOOPE',0,10,0e1h, 'LOOPZ',0,10,0e1h
    db 'LOOPNE',0,10,0e0h, 'LOOPNZ',0,10,0e0h, 'JCXZ',0,10,0e3h
    db 'RET',0,0,0c3h, 'RETF',0,0,0cbh, 'IRET',0,0,0cfh
    db 'NOP',0,0,90h, 'HLT',0,0,0f4h
    db 'CLD',0,0,0fch, 'STD',0,0,0fdh, 'CLI',0,0,0fah, 'STI',0,0,0fbh
    db 'CLC',0,0,0f8h, 'STC',0,0,0f9h, 'CMC',0,0,0f5h
    db 'LODSB',0,0,0ach, 'LODSW',0,0,0adh, 'STOSB',0,0,0aah
    db 'STOSW',0,0,0abh, 'MOVSB',0,0,0a4h, 'MOVSW',0,0,0a5h
    db 'CMPSB',0,0,0a6h, 'CMPSW',0,0,0a7h, 'SCASB',0,0,0aeh
    db 'SCASW',0,0,0afh
    db 'CBW',0,0,98h, 'CWD',0,0,99h, 'PUSHF',0,0,9ch, 'POPF',0,0,9dh
    db 'LAHF',0,0,9fh, 'SAHF',0,0,9eh, 'XLAT',0,0,0d7h
    db 'REP',0,1,0f3h, 'REPE',0,1,0f3h, 'REPZ',0,1,0f3h
    db 'REPNE',0,1,0f2h, 'REPNZ',0,1,0f2h
    db 'XCHG',0,13,0, 'LEA',0,14,0, 'IN',0,15,0, 'OUT',0,16,0
    db 0

k_assume  db 'ASSUME',0
k_end     db 'END',0
k_org     db 'ORG',0
k_db      db 'DB',0
k_dw      db 'DW',0
k_equ     db 'EQU',0
k_segment db 'SEGMENT',0
k_ends    db 'ENDS',0
k_endp    db 'ENDP',0
k_proc    db 'PROC',0
k_dup     db 'DUP',0
k_offset  db 'OFFSET',0
k_dollar  db '$',0
k_byte    db 'BYTE',0
k_word    db 'WORD',0
k_ptr     db 'PTR',0
k_short   db 'SHORT',0
k_near    db 'NEAR',0

e_instr   db 'unknown instruction',0
e_extra   db 'something is left over on the line',0
e_twice   db 'this name is already used',0
e_phase   db 'a name moved between passes',0
e_names   db 'too many names',0
e_name    db 'unknown name',0
e_number  db 'bad number',0
e_string  db 'a quote is missing',0
e_value   db 'a number or name is expected',0
e_ops     db 'these operands do not fit',0
e_addr    db 'bad address between [ ]',0
e_sizes   db 'a byte and a word are mixed',0
e_size    db 'say BYTE PTR or WORD PTR',0
e_shift   db 'shift by 1 or by CL',0
e_far     db 'too far for this jump: use JMP',0
e_big     db 'the program is too large',0

t_title   db 'WinKit Code',0
t_bnew    db 'New',0
t_bopen   db 'Open',0
t_bsave   db 'Save',0
t_bbuild  db 'Build',0
t_brun    db 'Run',0
t_noname  db '(no name)',0
t_asm     db '.ASM',0
t_wkx     db '.WKX',0
t_ready   db 'F2 Save  F3 Open  F5 Build  F9 Run  Esc Quit',0
t_new     db 'A new program. F9 builds and runs it.',0
t_full    db 'The text is full.',0
t_unsaved db 'Not saved. Esc again leaves; F2 saves.',0
t_cancel  db 'Cancelled.',0
t_askopen db 'Open (name, Enter): ',0
t_asksave db 'Save as (name, Enter): ',0
t_newfile db 'No such file yet: this will be a new one.',0
t_opened  db 'Opened ',0
t_saved   db 'Saved ',0
t_diskerr db 'Could not save: disk error or disk full.',0
t_line    db 'Line ',0
t_colon   db ': ',0
t_built   db 'Built ',0
t_comma   db ', ',0
t_bytes   db ' bytes. F9 runs it.',0
t_norun   db 'The program could not be started.',0

; What New gives you: a complete program.
template db '; A WinKit program. F9 builds and runs it.',10
    db 'start:',10
    db '    mov dx,OFFSET hello',10
    db '    mov ah,2            ; print the text at DS:DX',10
    db '    int 80h',10
    db '    xor ah,ah           ; exit',10
    db '    int 80h',10
    db 10
    db 'hello db ',27h,'Hello from my own program!',27h,',13,10,0',10
    db 0

; Not part of the file: the text (TEXT_MAX), the work area, the names.
heap:
CODE ENDS
END start
