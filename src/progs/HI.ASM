; HI.WKX - the quick-start program from docs/WKX-PROGRAMMING.md.
CODE SEGMENT PARA PUBLIC 'CODE'
ASSUME CS:CODE, DS:CODE
ORG 100h

start:
    mov dx,OFFSET msg      ; DS:DX -> NUL-terminated string
    mov ah,2               ; AH=2: print string
    int 80h
    xor ah,ah              ; AH=0: exit
    int 80h

msg db 'Hi from WinKit!',13,10,0
CODE ENDS
END start
