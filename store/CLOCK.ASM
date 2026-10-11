; CLOCK.WKX - prints the date and time of the BIOS real-time clock.
; Store: Date and time from the clock
CODE SEGMENT PARA PUBLIC 'CODE'
ASSUME CS:CODE, DS:CODE
ORG 100h

start:
    mov dx,OFFSET t_date
    mov ah,2
    int 80h
    mov ah,4                           ; CH century, CL year, DH month, DL day
    int 1ah
    mov al,ch
    call bcd
    mov al,cl
    call bcd
    mov al,'-'
    call putc
    mov al,dh
    call bcd
    mov al,'-'
    call putc
    mov al,dl
    call bcd
    mov dx,OFFSET t_time
    mov ah,2
    int 80h
    mov ah,2                           ; CH hour, CL minute, DH second
    int 1ah
    mov al,ch
    call bcd
    mov al,':'
    call putc
    mov al,cl
    call bcd
    mov al,':'
    call putc
    mov al,dh
    call bcd
    mov dx,OFFSET t_end
    mov ah,2
    int 80h
    xor ah,ah                          ; exit
    int 80h

putc:
    push ax
    mov ah,1
    int 80h
    pop ax
    ret

; AL = BCD byte, printed as two digits.
bcd:
    push ax
    push cx
    mov ch,al
    mov cl,4
    shr al,cl
    add al,'0'
    call putc
    mov al,ch
    and al,0fh
    add al,'0'
    call putc
    pop cx
    pop ax
    ret

t_date db 'Date: ',0
t_time db 13,10,'Time: ',0
t_end  db 13,10,0
CODE ENDS
END start
