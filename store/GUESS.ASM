; GUESS.WKX - think of a number: the program picks one from 1 to 100 and
; says "higher" or "lower" until you find it.
; Store: Guess the number game
CODE SEGMENT PARA PUBLIC 'CODE'
ASSUME CS:CODE, DS:CODE
ORG 100h

start:
    mov dx,OFFSET t_head
    mov ah,2
    int 80h
    xor ah,ah                          ; BIOS tick count picks the number
    int 1ah
    mov ax,dx
    xor dx,dx
    mov cx,100
    div cx
    inc dx
    mov secret,dx
ask:
    mov dx,OFFSET t_ask
    mov ah,2
    int 80h
    xor bx,bx                          ; BX = the number typed so far
    xor cx,cx                          ; CX = digits typed
read:
    mov ah,3                           ; wait for a key
    int 80h
    cmp al,27
    je give_up
    cmp al,13
    je entered
    cmp al,'0'
    jb read
    cmp al,'9'
    ja read
    cmp cx,3
    jae read
    mov ah,1                           ; echo the digit
    int 80h
    sub al,'0'
    xor ah,ah
    push ax
    mov ax,bx
    mov dx,10
    mul dx
    mov bx,ax
    pop ax
    add bx,ax
    inc cx
    jmp read
entered:
    jcxz read                          ; nothing typed yet
    inc tries
    cmp bx,secret
    je found
    mov dx,OFFSET t_higher
    jb tell
    mov dx,OFFSET t_lower
tell:
    mov ah,2
    int 80h
    jmp ask
found:
    mov dx,OFFSET t_found
    mov ah,2
    int 80h
    mov ax,tries
    call print_number
    mov dx,OFFSET t_tries
    mov ah,2
    int 80h
    jmp done
give_up:
    mov dx,OFFSET t_was
    mov ah,2
    int 80h
    mov ax,secret
    call print_number
    mov dx,OFFSET t_crlf
    mov ah,2
    int 80h
done:
    xor ah,ah                          ; exit
    int 80h

; AX in decimal. Preserves every register.
print_number:
    push ax
    push bx
    push cx
    push dx
    mov bx,10
    xor cx,cx
digits:
    xor dx,dx
    div bx
    push dx
    inc cx
    or ax,ax
    jnz digits
emit:
    pop ax
    add al,'0'
    mov ah,1
    int 80h
    loop emit
    pop dx
    pop cx
    pop bx
    pop ax
    ret

secret dw 0
tries  dw 0
t_head   db 'I picked a number from 1 to 100. Esc gives up.',13,10,0
t_ask    db 'Your guess: ',0
t_higher db '  higher',13,10,0
t_lower  db '  lower',13,10,0
t_found  db '  yes! Found in ',0
t_tries  db ' tries.',13,10,0
t_was    db 13,10,'It was ',0
t_crlf   db 13,10,0
CODE ENDS
END start
