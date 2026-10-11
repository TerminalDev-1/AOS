; GUESS.WKX - a window game: the program picks a number from 1 to 100 and
; says "too low" or "too high" until you find it.
; Store: Guess the number game
CODE SEGMENT PARA PUBLIC 'CODE'
ASSUME CS:CODE, DS:CODE
ORG 100h

K_BLACK   EQU 0                        ; the WinKit palette
K_FACE    EQU 4
K_SHADOW  EQU 5
BV_BUTTON EQU 0                        ; bevel styles
BV_FIELD  EQU 2

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
    GTXT 20,20,K_BLACK,t_head
    GTXT 20,60,K_BLACK,t_ask
    GBEV 176,56,72,24,BV_BUTTON
    GTXT 192,60,K_BLACK,t_guess
    GBEV 256,56,96,24,BV_BUTTON
    GTXT 272,60,K_BLACK,t_new
    GTXT 20,150,K_SHADOW,t_keys
new_game:
    xor ah,ah                          ; BIOS tick count picks the number
    int 1ah
    mov ax,dx
    xor dx,dx
    mov cx,100
    div cx
    inc dx
    mov secret,dx
    mov tries,0
    mov di,OFFSET msg
    mov si,OFFSET t_start
    call put_str
    call show_msg
clear:
    mov ndig,0
    mov digits,0
    call show_field
wait:
    mov ah,14h                         ; wait for a key or a click
    int 80h
    cmp ax,3                           ; the close button
    je done
    cmp ax,2
    je on_click
    cmp ax,1
    jne wait
    cmp bl,27
    je done
    cmp bl,13
    je answer
    cmp bl,'n'
    je new_game
    cmp bl,8
    je erase
    cmp bl,'0'
    jb wait
    cmp bl,'9'
    ja wait
    cmp ndig,3
    jae wait
    mov si,ndig                        ; one more digit
    mov digits[si],bl
    inc si
    mov digits[si],0
    inc ndig
    call show_field
    jmp wait
erase:
    cmp ndig,0
    je wait
    dec ndig
    mov si,ndig
    mov digits[si],0
    call show_field
    jmp wait
done:
    xor ah,ah                          ; exit
    int 80h
on_click:
    sub cx,56                          ; both buttons are at y 56..79
    cmp cx,24
    jae to_wait
    cmp bx,176
    jb to_wait
    cmp bx,248
    jb answer
    cmp bx,256
    jb to_wait
    cmp bx,352
    jae to_wait
    jmp new_game
to_wait:
    jmp wait

; Compare what was typed with the secret number and say how it went.
answer:
    cmp ndig,0
    je to_wait
    xor ax,ax                          ; AX = the number typed
    mov si,OFFSET digits
an_digit:
    mov bl,[si]
    or bl,bl
    jz an_have
    inc si
    sub bl,'0'
    xor bh,bh
    mov cx,10
    mul cx
    add ax,bx
    jmp an_digit
an_have:
    inc tries
    mov di,OFFSET msg
    cmp ax,secret
    je an_found
    mov si,OFFSET t_low
    jb an_say
    mov si,OFFSET t_high
an_say:
    call put_dec
    call put_str
    call show_msg
    jmp clear
an_found:
    mov si,OFFSET t_yes
    call put_str
    call put_dec
    mov si,OFFSET t_in
    call put_str
    mov ax,tries
    call put_dec
    mov si,OFFSET t_tries
    call put_str
    call show_msg
    jmp clear

; The input field with the digits typed so far.
show_field:
    GBEV 116,56,48,24,BV_FIELD
    GTXT 124,60,K_BLACK,digits
    ret

; Ends the text in msg and shows it on the answer line.
show_msg:
    xor al,al
    stosb
    GBOX 20,104,380,16,K_FACE          ; wipe the old answer
    GTXT 20,104,K_BLACK,msg
    ret

; Append the NUL-terminated string at SI to DI.
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

; Append AX in decimal to DI.
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

secret dw 0
tries  dw 0
ndig   dw 0
digits db 4 dup (0)
msg    db 56 dup (0)

t_title db 'Guess the number',0
t_head  db 'I picked a number from 1 to 100.',0
t_ask   db 'Your guess:',0
t_guess db 'Guess',0
t_new   db 'New game',0
t_keys  db 'Type digits, Enter guesses, N starts again.',0
t_start db 'What is it?',0
t_low   db ' is too low.',0
t_high  db ' is too high.',0
t_yes   db 'Yes! ',0
t_in    db ' found in ',0
t_tries db ' tries.',0
CODE ENDS
END start
