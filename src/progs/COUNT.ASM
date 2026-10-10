; COUNT.WKX - counts from 1 to 20 using AH=1 (print character).
CODE SEGMENT PARA PUBLIC 'CODE'
ASSUME CS:CODE, DS:CODE
ORG 100h
start:
    mov dx,OFFSET head
    mov ah,2
    int 80h
    mov cx,1
next:
    mov ax,cx
    cmp cx,10
    jb ones
    mov al,'1'                         ; tens digit
    cmp cx,20
    jb tens
    mov al,'2'
tens:
    mov ah,1
    int 80h
    mov ax,cx
    sub ax,10
    cmp cx,20
    jb ones
    sub ax,10
ones:
    add al,'0'
    mov ah,1
    int 80h
    mov al,' '
    mov ah,1
    int 80h
    inc cx
    cmp cx,21
    jb next
    mov dx,OFFSET tail
    mov ah,2
    int 80h
    xor ah,ah
    int 80h
head db 'Counting: ',0
tail db 13,10,0
CODE ENDS
END start
