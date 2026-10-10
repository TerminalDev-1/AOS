; REVERSE.WKX - read a line, print it backwards.
; Shows AH=3 (read key), AH=1 (print character), and indexed data.
CODE SEGMENT PARA PUBLIC 'CODE'
ASSUME CS:CODE, DS:CODE
ORG 100h

start:
    mov dx,OFFSET ask
    mov ah,2
    int 80h
    xor si,si                  ; SI = number of characters read
read:
    mov ah,3                   ; wait for a key
    int 80h
    cmp al,13                  ; Enter ends the line
    je show
    cmp al,32
    jb read                    ; ignore control keys
    cmp si,40
    jae read                   ; line is full
    mov line[si],al            ; store it
    inc si
    mov ah,1                   ; echo it
    int 80h
    jmp read
show:
    mov dx,OFFSET crlf
    mov ah,2
    int 80h
back:
    or si,si
    jz done
    dec si
    mov al,line[si]
    mov ah,1
    int 80h
    jmp back
done:
    mov dx,OFFSET crlf
    mov ah,2
    int 80h
    xor ah,ah
    int 80h

ask  db 'Type a line: ',0
crlf db 13,10,0
line db 40 dup (0)
CODE ENDS
END start
