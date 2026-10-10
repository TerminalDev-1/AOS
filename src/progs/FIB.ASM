; FIB.WKX - prints Fibonacci numbers. Shows how to print a number with
; nothing but INT 80h: divide by 10, push the digits, pop them in order.
CODE SEGMENT PARA PUBLIC 'CODE'
ASSUME CS:CODE, DS:CODE
ORG 100h

start:
    mov dx,OFFSET head
    mov ah,2                           ; print the string at DS:DX
    int 80h
    xor si,si                          ; a = 0
    mov di,1                           ; b = 1
    mov cx,13                          ; how many numbers
next:
    mov ax,si
    call print_number
    mov al,' '
    mov ah,1                           ; print the character in AL
    int 80h
    mov ax,si                          ; a, b = b, a + b
    add ax,di
    mov si,di
    mov di,ax
    loop next
    mov dx,OFFSET tail
    mov ah,2
    int 80h
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
    div bx                             ; DX = next digit, AX = rest
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

head db 'Fibonacci: ',0
tail db 13,10,0
CODE ENDS
END start
