; POWER.INC - leaving the desktop: restart or power off. Nothing here
; touches DOS; the BIOS does the work.

disable_mouse PROC NEAR
    cmp mouseok,0
    je dm_done
    mov ax,0c200h
    xor bh,bh
    int 15h
    mov ax,0c207h
    xor bx,bx
    mov es,bx
    int 15h
dm_done:
    ret
disable_mouse ENDP

reboot:
    call disable_mouse
    int 19h
    jmp reboot

; APM 1.2: negotiate and request system off. The fallback stays graphical.
poweroff:
    mov ax,5301h
    xor bx,bx
    int 15h
    mov ax,530eh
    xor bx,bx
    mov cx,0102h
    int 15h
    mov ax,5307h
    mov bx,1
    mov cx,3
    int 15h
    mov viewstate,V_OFF
    mov dirty,1
    ret
