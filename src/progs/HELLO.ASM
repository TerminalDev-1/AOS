; HELLO.WKX - the smallest WinKit program.
; Programs are flat binaries (ORG 100h) loaded at 3000:0100. Services are
; reached with INT 80h:
;   AH=0 exit            AH=1 print char AL
;   AH=2 print DS:DX     AH=3 wait for a key (AL = ASCII, AH = scan code)
CODE SEGMENT PARA PUBLIC 'CODE'
ASSUME CS:CODE, DS:CODE
ORG 100h
start:
    mov dx,OFFSET msg
    mov ah,2
    int 80h
    xor ah,ah
    int 80h
msg db 'Hello from a native WinKit program!',13,10,0
CODE ENDS
END start
