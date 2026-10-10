; MACROS.INC - long conditional jumps and drawing helpers.
; The 8086 Jcc instructions only reach +-127 bytes; L_Jcc reaches anywhere.

L_JE MACRO target
    LOCAL skip
    jne skip
    jmp target
skip:
ENDM

L_JNE MACRO target
    LOCAL skip
    je skip
    jmp target
skip:
ENDM

L_JZ MACRO target
    LOCAL skip
    jnz skip
    jmp target
skip:
ENDM

L_JNZ MACRO target
    LOCAL skip
    jz skip
    jmp target
skip:
ENDM

L_JC MACRO target
    LOCAL skip
    jnc skip
    jmp target
skip:
ENDM

L_JNC MACRO target
    LOCAL skip
    jc skip
    jmp target
skip:
ENDM

L_JB MACRO target
    LOCAL skip
    jae skip
    jmp target
skip:
ENDM

L_JAE MACRO target
    LOCAL skip
    jb skip
    jmp target
skip:
ENDM

L_JA MACRO target
    LOCAL skip
    jbe skip
    jmp target
skip:
ENDM

L_JBE MACRO target
    LOCAL skip
    ja skip
    jmp target
skip:
ENDM

L_JL MACRO target
    LOCAL skip
    jge skip
    jmp target
skip:
ENDM

L_JLE MACRO target
    LOCAL skip
    jg skip
    jmp target
skip:
ENDM

L_JG MACRO target
    LOCAL skip
    jle skip
    jmp target
skip:
ENDM

L_JGE MACRO target
    LOCAL skip
    jl skip
    jmp target
skip:
ENDM

L_JNS MACRO target
    LOCAL skip
    js skip
    jmp target
skip:
ENDM

; ---- screen-space drawing (pixel coordinates) ----

; Filled rectangle.
BOX MACRO x,y,w,h,c
    mov rx,x
    mov ry,y
    mov rw,w
    mov rh,h
    mov ink,c
    call rect
ENDM

; 3D bevel (style m) around x,y,w,h.
BVL MACRO x,y,w,h,m
    mov bvx,x
    mov bvy,y
    mov bvw,w
    mov bvh,h
    mov bvm,m
    call bevel
ENDM

; Left-aligned label at pixel (x,y).
TXT MACRO x,y,c,s
    mov tx,x
    mov ty,y
    mov fg,c
    mov si,OFFSET s
    call textout
ENDM

; Bold label: the same text drawn twice, one pixel apart.
BTXT MACRO x,y,c,s
    TXT x,y,c,s
    TXT x+1,y,c,s
ENDM

; Label centred on pixel column x.
TXTC MACRO x,y,c,s
    mov ctrx,x
    mov ty,y
    mov fg,c
    mov si,OFFSET s
    call textc
ENDM

; Scaled label (bsc = pixel multiplier) at pixel (x,y).
TXTBIG MACRO x,y,c,sc,s
    mov tx,x
    mov ty,y
    mov fg,c
    mov bsc,sc
    mov si,OFFSET s
    call bigtext
ENDM

; ---- window-relative drawing (offsets from the origin wox/woy) ----

WBOX MACRO ox,oy,w,h,c
    mov ax,wox
    add ax,ox
    mov rx,ax
    mov ax,woy
    add ax,oy
    mov ry,ax
    mov rw,w
    mov rh,h
    mov ink,c
    call rect
ENDM

WBEV MACRO ox,oy,w,h,m
    mov ax,wox
    add ax,ox
    mov bvx,ax
    mov ax,woy
    add ax,oy
    mov bvy,ax
    mov bvw,w
    mov bvh,h
    mov bvm,m
    call bevel
ENDM

WTXT MACRO ox,oy,c,s
    mov ax,wox
    add ax,ox
    mov tx,ax
    mov ax,woy
    add ax,oy
    mov ty,ax
    mov fg,c
    mov si,OFFSET s
    call textout
ENDM

WBTXT MACRO ox,oy,c,s
    WTXT ox,oy,c,s
    WTXT ox+1,oy,c,s
ENDM

; Same, with the string already in SI.
WTXTR MACRO ox,oy,c
    mov ax,wox
    add ax,ox
    mov tx,ax
    mov ax,woy
    add ax,oy
    mov ty,ax
    mov fg,c
    call textout
ENDM

; ---- icon-relative drawing (offsets from icx/icy) ----

IBOX MACRO ox,oy,w,h,c
    mov ax,icx
    add ax,ox
    mov rx,ax
    mov ax,icy
    add ax,oy
    mov ry,ax
    mov rw,w
    mov rh,h
    mov ink,c
    call rect
ENDM
