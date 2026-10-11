; WinKit-preview: the desktop and the WinKit terminal - a standalone 16-bit
; system.
; Real-mode 8086 assembly (MASM 5.10), no libraries. It draws straight to
; planar VGA memory in mode 12h (640x480, 16 colours) and uses a BIOS PS/2
; callback for the mouse and BIOS disk services for the FAT12 volume, so
; there is no DOS underneath: BOOT.ASM loads this kernel straight from the
; reserved sectors. The look is a classic 3D desktop. One flat binary,
; built from these modules (all pulled in with INCLUDE):
;
;   MACROS.INC   long jumps and drawing macros
;   CONST.INC    palette indices, layout, action codes
;   DATA.INC     state, button tables, strings, palette, bitmaps
;   GFX.INC      rectangles, text, pointer, partial repaint, window blit
;   VIDEO.INC    the QEMU display driver: a wider 16-colour screen
;   WIDGETS.INC  bevels, title gradient, window chrome, icons, buttons
;   SCENES.INC   boot screen, desktop, taskbar, Start menu, the two dialogs
;   WINDOWS.INC  File Manager/Notes/System bodies, minimize, dragging
;   INPUT.INC    mouse callback, keyboard, click and action dispatch
;   VMMOUSE.INC  the QEMU mouse driver: absolute pointer positions
;   FS.INC       FAT12 driver over BIOS INT 13h
;   STORAGE.INC  the File Manager's list and actions, NOTES.TXT
;   TERM.INC     the WinKit terminal and INT 80h program interface
;   PGUI.INC     INT 80h window, drawing and events for programs
;   POWER.INC    restart, power off
CODE SEGMENT PARA PUBLIC 'CODE'
ASSUME CS:CODE, DS:CODE
ORG 100h

INCLUDE MACROS.INC
INCLUDE CONST.INC

entry:
    jmp start

INCLUDE DATA.INC

start:
    cld
    push cs
    pop ds
    mov ax,1130h                       ; BIOS 8x16 font
    mov bh,6
    int 10h
    mov fontoff,bp
    mov fontseg,es
    mov ax,12h
    int 10h
    call video_init                    ; wider, if the card can
    call set_palette
    mov ax,0a000h
    mov es,ax
    mov dx,3ceh                        ; set/reset on all planes, replace, all bits
    mov ax,0f01h
    out dx,ax
    mov ax,0003h
    out dx,ax
    mov ax,0ff08h
    out dx,ax
    call boot_screen                   ; the logo, while the system starts
    call sys_install                   ; INT 80h for native programs
    call fs_init                       ; FAT12 volume
    call term_init
    call load_note
    call refresh_files
    mov ax,0c205h                      ; PS/2 packet size 3, callback, enable
    mov bh,3
    int 15h
    L_JC no_mouse
    push cs
    pop es
    mov bx,OFFSET mouse_callback
    mov ax,0c207h
    int 15h
    L_JC no_mouse
    mov ax,0c200h
    mov bh,1
    int 15h
    L_JC no_mouse
    mov mouseok,1
    call vmm_init                      ; absolute positions, under QEMU
no_mouse:
    mov ax,0a000h
    mov es,ax
    call boot_wait                     ; the bar fills, then the desktop

mainloop:
    cmp dirty,0
    L_JE window_moved
    mov dirty,0                        ; something changed: repaint the scene
    mov curdirty,0
    mov winmoved,0
    call paint
    jmp input
window_moved:
    cmp winmoved,0                     ; a dragged window: blit it
    L_JE pointer_only
    mov winmoved,0
    mov curdirty,0
    call blit_window
    jmp input
pointer_only:
    cmp termdirty,0                    ; terminal text changed
    L_JE pointer_only2
    mov termdirty,0
    cmp dirty,0
    L_JNE pointer_only2
    call term_update
pointer_only2:
    cmp curdirty,0                     ; just the arrow moved
    L_JE input
    mov curdirty,0
    call cursor_move
    cmp menuopen,0                     ; keep the menu highlight current
    L_JE input
    call menu_hover
input:
    mov al,buttons
    cmp al,lastbuttons
    L_JE keyboard
    mov lastbuttons,al
    test al,1
    L_JZ release_mouse
    call click
    jmp keyboard
release_mouse:
    mov drag,0
keyboard:
    cmp drag,0
    L_JE key_poll
    call move_window
key_poll:
    mov ah,1
    int 16h
    L_JZ idle
    mov ah,0
    int 16h
    call key_input
    jmp mainloop
idle:
    call check_clock                   ; once a minute the tray clock changes
    cmp clockdirty,0
    L_JE idle_wait
    mov clockdirty,0
    cmp dirty,0                        ; a full repaint will draw it anyway
    L_JNE idle_wait
    call restore_under
    mov ax,clockx                      ; the tray clock's corner
    sub ax,4
    mov bx,TASK_Y
    mov cx,scrw
    mov dx,SCR_H
    call repaint_area
    call cursor_show
idle_wait:
    sti                                ; let the mouse callback run
    hlt
    jmp mainloop

INCLUDE GFX.INC
INCLUDE VIDEO.INC
INCLUDE WIDGETS.INC
INCLUDE SCENES.INC
INCLUDE WINDOWS.INC
INCLUDE INPUT.INC
INCLUDE VMMOUSE.INC
INCLUDE FS.INC
INCLUDE STORAGE.INC
INCLUDE TERM.INC
INCLUDE PGUI.INC
INCLUDE POWER.INC

CODE ENDS
END entry
