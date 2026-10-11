; DICE.WKX - a window with two dice. Click Roll (or press Space) to roll.
; Store: Roll two dice
CODE SEGMENT PARA PUBLIC 'CODE'
ASSUME CS:CODE, DS:CODE
ORG 100h

K_BLACK   EQU 0                        ; the WinKit palette
K_FACE    EQU 4
K_SHADOW  EQU 5
BV_BUTTON EQU 0                        ; bevel styles
BV_FIELD  EQU 2

DIE_Y     EQU 40                       ; top of both dice
DIE_SIZE  EQU 80
PIP       EQU 12                       ; a pip is a 12 x 12 square

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
    mov dx,OFFSET t_title
    mov ah,10h                         ; open the window
    int 80h
    GBEV 163,180,84,24,BV_BUTTON
    GTXT 189,184,K_BLACK,t_roll
    GTXT 101,224,K_SHADOW,t_hint
    xor ah,ah                          ; seed from the BIOS tick count
    int 1ah
    mov seed,dx
roll:
    xor ah,ah                          ; when you roll adds to the luck
    int 1ah
    add seed,dx
    call die
    mov total,al
    mov diex,105
    call draw_die
    call die
    add total,al
    mov diex,225
    call draw_die
    mov al,total                       ; "Total: n"
    xor ah,ah
    mov cl,10
    div cl                             ; AL = tens, AH = units
    mov di,OFFSET t_num
    or al,al
    jz units
    add al,'0'
    stosb
units:
    mov al,ah
    add al,'0'
    stosb
    xor al,al
    stosb
    GBOX 150,140,120,16,K_FACE         ; wipe the old total
    GTXT 169,140,K_BLACK,t_total
wait:
    mov ah,14h                         ; wait for a key or a click
    int 80h
    cmp ax,3                           ; the close button
    je done
    cmp ax,1
    jne not_key
    cmp bl,27
    je done
    cmp bl,' '
    je roll
    cmp bl,13
    je roll
    jmp wait
not_key:
    cmp ax,2                           ; a click: on Roll?
    jne wait
    sub bx,163
    cmp bx,84
    jae wait
    sub cx,180
    cmp cx,24
    jae wait
    jmp roll
done:
    xor ah,ah                          ; exit
    int 80h

; AL = 1..6.
die:
    push cx
    push dx
    mov ax,seed
    mov cx,25173
    mul cx
    add ax,13849
    mov seed,ax
    mov al,ah                          ; the high bits are the random ones
    xor ah,ah
    mov cl,6
    div cl
    mov al,ah
    inc al
    pop dx
    pop cx
    ret

; Draws the face AL (1..6) with its left edge at diex.
draw_die:
    push ax
    mov bl,al
    xor bh,bh
    dec bx
    mov al,faces[bx]
    mov pips,al
    GBEV diex,DIE_Y,DIE_SIZE,DIE_SIZE,BV_FIELD
    mov pipn,0
dd_next:
    shr pips,1                         ; one bit for each of the 7 places
    jnc dd_skip
    mov bx,pipn
    shl bx,1
    mov cx,pipy[bx]
    add cx,DIE_Y
    mov bx,pipx[bx]
    add bx,diex
    mov si,PIP
    mov di,PIP
    mov al,K_BLACK
    mov ah,11h
    int 80h
dd_skip:
    inc pipn
    cmp pipn,7
    jb dd_next
    pop ax
    ret

seed  dw 0
diex  dw 0
pipn  dw 0
pips  db 0
total db 0

; Pip places: top-left, top-right, left, centre, right, bottom-left,
; bottom-right; and which of them each face shows (bit 0 = the first).
pipx  dw 14,54,14,34,54,14,54
pipy  dw 14,14,34,34,34,54,54
faces db 08h,41h,49h,63h,6bh,77h

t_title db 'Dice',0
t_roll  db 'Roll',0
t_hint  db 'Click Roll or press Space.',0
t_total db 'Total: '
t_num   db 0,0,0
CODE ENDS
END start
