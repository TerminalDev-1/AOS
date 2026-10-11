; DICE.WKX - rolls two dice each time a key is pressed.
; Store: Roll two dice
CODE SEGMENT PARA PUBLIC 'CODE'
ASSUME CS:CODE, DS:CODE
ORG 100h

start:
    mov dx,OFFSET t_head
    mov ah,2
    int 80h
    xor ah,ah                          ; seed from the BIOS tick count
    int 1ah
    mov seed,dx
roll:
    mov ah,3                           ; wait for a key
    int 80h
    cmp al,27
    je done
    xor ah,ah                          ; when the key came adds to the luck
    int 1ah
    add seed,dx
    mov dx,OFFSET t_roll
    mov ah,2
    int 80h
    call die
    mov bl,al
    call digit
    mov dx,OFFSET t_and
    mov ah,2
    int 80h
    call die
    add bl,al
    call digit
    mov dx,OFFSET t_makes
    mov ah,2
    int 80h
    mov al,bl
    xor ah,ah
    mov cl,10
    div cl                             ; AL = tens, AH = units
    mov ch,ah
    or al,al
    jz units
    call digit
units:
    mov al,ch
    call digit
    mov dx,OFFSET t_crlf
    mov ah,2
    int 80h
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

; Prints AL (0..9) as a digit.
digit:
    push ax
    add al,'0'
    mov ah,1
    int 80h
    pop ax
    ret

seed dw 0
t_head  db 'Press a key to roll, Esc to stop.',13,10,0
t_roll  db 'You rolled ',0
t_and   db ' and ',0
t_makes db ', that makes ',0
t_crlf  db 13,10,0
CODE ENDS
END start
