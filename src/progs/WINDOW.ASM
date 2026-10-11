; WINDOW.WKX - the minimum windowed program: opens a window with one
; button and waits until the button or the close box is clicked.
CODE SEGMENT PARA PUBLIC 'CODE'
ASSUME CS:CODE, DS:CODE
ORG 100h

start:
    mov dx,OFFSET caption
    mov ah,10h                         ; open the window
    int 80h
    mov bx,20                          ; a raised button at 20,20, 96 x 24
    mov cx,20
    mov si,96
    mov di,24
    mov al,0
    mov ah,13h
    int 80h
    mov bx,52                          ; its label, in black
    mov cx,24
    mov dx,OFFSET quit
    mov al,0
    mov ah,12h
    int 80h
again:
    mov ah,14h                         ; AX = event, BX,CX = details
    int 80h
    cmp ax,3                           ; the close button
    je done
    cmp ax,2                           ; a click: on our button?
    jne again
    sub bx,20
    cmp bx,96
    jae again
    sub cx,20
    cmp cx,24
    jae again
done:
    mov dx,OFFSET bye                  ; shows in the terminal afterwards
    mov ah,2
    int 80h
    xor ah,ah                          ; exit: the window closes
    int 80h

caption db 'Hello window',0
quit    db 'Quit',0
bye     db 'The window is closed.',13,10,0
CODE ENDS
END start
