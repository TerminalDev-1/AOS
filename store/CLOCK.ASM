; CLOCK.WKX - a window with the date and time of the BIOS real-time clock,
; kept up to date once a second.
; Store: Date and time, ticking
CODE SEGMENT PARA PUBLIC 'CODE'
ASSUME CS:CODE, DS:CODE
ORG 100h

K_BLACK   EQU 0                        ; the WinKit palette
K_SHADOW  EQU 5
K_WHITE   EQU 6
BV_BUTTON EQU 0                        ; bevel styles
BV_FIELD  EQU 2

; Filled rectangle.
GBOX MACRO x,y,w,h,c
    mov bx,x
    mov cx,y
    mov si,w
    mov di,h
    mov al,c
    mov ah,11h
    int 80h
ENDM

; 3D box in bevel style m.
GBEV MACRO x,y,w,h,m
    mov bx,x
    mov cx,y
    mov si,w
    mov di,h
    mov al,m
    mov ah,13h
    int 80h
ENDM

; The string s at x,y in colour c.
GTXT MACRO x,y,c,s
    mov bx,x
    mov cx,y
    mov dx,OFFSET s
    mov al,c
    mov ah,12h
    int 80h
ENDM

start:
    mov dx,OFFSET t_title
    mov ah,10h                         ; open the window
    int 80h
    GBEV 95,60,220,96,BV_FIELD
    GTXT 123,84,K_SHADOW,t_date
    GTXT 123,116,K_SHADOW,t_time
    GBEV 163,190,84,24,BV_BUTTON
    GTXT 185,194,K_BLACK,t_close
tick:
    call show
    mov ah,15h                         ; an event, or 0 after one timer tick
    int 80h
    cmp ax,3                           ; the close button
    je done
    cmp ax,1
    jne not_key
    cmp bl,27
    je done
    cmp bl,13
    je done
    jmp tick
not_key:
    cmp ax,2                           ; a click: on Close?
    jne tick
    sub bx,163
    cmp bx,84
    jae tick
    sub cx,190
    cmp cx,24
    jae tick
done:
    xor ah,ah                          ; exit
    int 80h

; Redraws the date and time when the second has changed.
show:
    mov ah,2                           ; CH hour, CL minute, DH second (BCD)
    int 1ah
    cmp dh,lastsec
    jne sh_new
    ret
sh_new:
    mov lastsec,dh
    mov di,OFFSET timebuf
    mov al,ch
    call bcd
    mov al,':'
    stosb
    mov al,cl
    call bcd
    mov al,':'
    stosb
    mov al,dh
    call bcd
    xor al,al
    stosb
    mov ah,4                           ; CH century, CL year, DH month, DL day
    int 1ah
    mov di,OFFSET datebuf
    mov al,ch
    call bcd
    mov al,cl
    call bcd
    mov al,'-'
    stosb
    mov al,dh
    call bcd
    mov al,'-'
    stosb
    mov al,dl
    call bcd
    xor al,al
    stosb
    GBOX 179,84,100,16,K_WHITE         ; text has no background: wipe first
    GTXT 179,84,K_BLACK,datebuf
    GBOX 179,116,100,16,K_WHITE
    GTXT 179,116,K_BLACK,timebuf
    ret

; AL = BCD byte: two digits are written at DI.
bcd:
    push ax
    push cx
    mov ah,al
    mov cl,4
    shr al,cl
    add al,'0'
    stosb
    mov al,ah
    and al,0fh
    add al,'0'
    stosb
    pop cx
    pop ax
    ret

lastsec db 0ffh
datebuf db 12 dup (0)
timebuf db 12 dup (0)
t_title db 'Clock',0
t_date  db 'Date:',0
t_time  db 'Time:',0
t_close db 'Close',0
CODE ENDS
END start
