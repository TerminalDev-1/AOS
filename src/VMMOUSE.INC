; VMMOUSE.INC - the QEMU mouse driver.
;
; A PS/2 mouse only reports movement, so the pointer WinKit draws and the
; host's own pointer drift apart and the host's leaves the emulator window
; before WinKit's reaches the edge. QEMU also offers the "VMware mouse": the
; guest asks for absolute positions and from then on reads where the host
; pointer is inside the window, as x and y from 0 to 65535. With this driver
; WinKit's pointer sits exactly under the host's and no mouse grab is needed.
;
; The interface is a "backdoor" I/O port: IN EAX,DX with EAX = a magic
; number, ECX = a command and DX = 5658h. It needs 32-bit registers, which
; the 8086 instruction set (and wkasm) does not have, so the few 386
; instructions are written as bytes. That is safe here: the port only
; answers on a machine that emulates it, and such a machine is not an 8086.
;
; The PS/2 mouse stays on: every absolute report still arrives with a PS/2
; packet, which is what makes the BIOS call mouse_callback (INPUT.INC). The
; callback asks this driver first and only uses the packet's own movement
; when there is no absolute report (a real mouse, or another emulator).

VMM_VERSION EQU 10                     ; backdoor commands
VMM_DATA    EQU 39
VMM_STATUS  EQU 40
VMM_COMMAND EQU 41

; EBX = hi:lo.
VMM_EBX MACRO lo,hi
    db 66h,0bbh                        ; mov ebx,imm32
    dw lo,hi
ENDM

; One backdoor call. EBX is the argument; results come back in EAX..EDX.
VMM_CALL MACRO cmd
    db 66h,0b8h,68h,58h,4dh,56h        ; mov eax,564D5868h ("VMXh")
    db 66h,0b9h                        ; mov ecx,cmd
    dw cmd,0
    mov dx,5658h
    db 66h,0edh                        ; in eax,dx
ENDM

; Look for the interface and ask for absolute positions. Sets vmmouse.
vmm_init PROC NEAR
    db 66h,60h                         ; pushad
    VMM_EBX 0,0
    VMM_CALL VMM_VERSION               ; EBX = the magic number, if present
    cmp bx,5868h
    jne vmi_done
    VMM_EBX 4552h,4541h                ; "READ_ID"
    VMM_CALL VMM_COMMAND
    VMM_EBX 0,0
    VMM_CALL VMM_STATUS                ; AX = words waiting
    or ax,ax
    jz vmi_done
    VMM_EBX 1,0
    VMM_CALL VMM_DATA                  ; EAX = 3442554Ah, the mouse's ID
    cmp ax,554ah
    jne vmi_done
    VMM_EBX 4152h,5342h                ; "REQUEST_ABSOLUTE"
    VMM_CALL VMM_COMMAND
    mov vmmouse,1
vmi_done:
    db 66h,61h                         ; popad
    ret
vmm_init ENDP

; Take every waiting report: mx, my and buttons follow the last one.
; ZF clear when there was at least one. Called from the mouse callback.
vmm_read PROC NEAR
    db 66h,60h                         ; pushad
    mov vmgot,0
vmr_next:
    VMM_EBX 0,0
    VMM_CALL VMM_STATUS                ; AX = words waiting
    cmp ax,4
    jb vmr_done
    VMM_EBX 4,0
    VMM_CALL VMM_DATA                  ; AX = buttons, BX = x, CX = y
    mov si,ax
    mov ax,bx
    mul scrw                           ; DX = x * width / 65536
    cmp dx,maxx
    jbe vmr_x
    mov dx,maxx
vmr_x:
    cmp dx,mx
    je vmr_y
    mov mx,dx
    mov curdirty,1
vmr_y:
    mov ax,cx
    mov cx,SCR_H
    mul cx
    cmp dx,my
    je vmr_btn
    mov my,dx
    mov curdirty,1
vmr_btn:
    xor al,al
    test si,20h                        ; left button
    jz vmr_set
    inc al
vmr_set:
    mov buttons,al
    mov vmgot,1
    jmp vmr_next
vmr_done:
    db 66h,61h                         ; popad
    cmp vmgot,0
    ret
vmm_read ENDP
