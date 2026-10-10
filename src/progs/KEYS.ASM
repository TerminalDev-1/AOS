; KEYS.WKX - echoes keys until Esc, using AH=3 (wait for a key).
CODE SEGMENT PARA PUBLIC 'CODE'
ASSUME CS:CODE, DS:CODE
ORG 100h
start:
    mov dx,OFFSET head
    mov ah,2
    int 80h
again:
    mov ah,3
    int 80h
    cmp al,27
    je done
    cmp al,32
    jb again
    mov ah,1
    int 80h
    jmp again
done:
    mov dx,OFFSET tail
    mov ah,2
    int 80h
    xor ah,ah
    int 80h
head db 'Type something, Esc quits: ',0
tail db 13,10,'bye',13,10,0
CODE ENDS
END start
