; CONST.INC - palette, layout and action constants.

; ---- palette (programmed into the VGA DAC at start-up, see DATA.INC) ----
K_BLACK   EQU 0
K_NAVY    EQU 1      ; title bar, selection
K_SKY     EQU 2      ; title bar gradient end
K_TEAL    EQU 3      ; desktop
K_FACE    EQU 4      ; window / button face (silver)
K_SHADOW  EQU 5      ; dark grey
K_WHITE   EQU 6
K_LIGHT   EQU 7      ; light grey bevel edge
K_INACT   EQU 8      ; inactive title gradient end
K_RED     EQU 9
K_YELLOW  EQU 10
K_GREEN   EQU 11
K_MID     EQU 12     ; title gradient midpoint
K_BLUE    EQU 13
K_ORANGE  EQU 14
K_CYAN    EQU 15

; ---- layout (pixels) ----
SCR_W     EQU 848    ; the widest screen: full-width fills use this and
                     ; are clipped to the real width, scrw (VIDEO.INC)
SCR_H     EQU 480
TASK_Y    EQU 452
TASK_H    EQU 28
WIN_W     EQU 416    ; must stay a multiple of 8: windows move by byte
WIN_H     EQU 288
TBAR_H    EQU 18
WIN_YMIN  EQU 0
WIN_YMAX  EQU 164
PROGSEG   EQU 3000h  ; native programs run here
GUI_Y     EQU 72     ; a program's window (see PGUI.INC); x is guix
GUI_CX    EQU 3      ; its client origin inside the frame
GUI_CY    EQU 21

MENU_X    EQU 2
MENU_Y    EQU 241
MENU_W    EQU 176
MENU_H    EQU 211
SUB_X     EQU 178    ; the Programs list beside the Start menu
SUB_W     EQU 152
SUB_ROW   EQU 18
SUB_MAX   EQU 16     ; programs it can show
DESK_SYS  EQU 5      ; desktop icons of the system itself
DESK_MAX  EQU 15     ; program shortcuts: three more columns of five
FM_ROWS   EQU 9      ; files on a page of the File Manager

WEL_X     EQU 120    ; dialogs: where they are on a 640 screen; dlgx
                     ; is added to centre them on a wider one
WEL_Y     EQU 96
WEL_W     EQU 400
WEL_H     EQU 250
SHD_X     EQU 140
SHD_Y     EQU 110
SHD_W     EQU 360
SHD_H     EQU 250

; ---- bevel styles ----
BV_BUTTON EQU 0      ; raised push button
BV_WINDOW EQU 1      ; raised window frame
BV_FIELD  EQU 2      ; sunken white field
BV_PANEL  EQU 3      ; one-pixel sunken status panel
BV_PRESS  EQU 4      ; pressed push button

; ---- button table entries (12 bytes) ----
B_X       EQU 0
B_Y       EQU 2
B_W       EQU 4
B_H       EQU 6
B_STYLE   EQU 8
B_ACT     EQU 9
B_LBL     EQU 10
B_SIZE    EQU 12

S_PUSH    EQU 0
S_DEFAULT EQU 1
S_RADIO   EQU 2

; ---- actions ----
A_NONE     EQU 0
A_FILES    EQU 1     ; 1..4 double as window numbers
A_NOTES    EQU 2
A_SYSTEM   EQU 3
A_TERM     EQU 4     ; the WinKit terminal
A_SHUTDOWN EQU 18    ; open the Shut Down dialog
A_STORE    EQU 19    ; run WKSTORE.WKX
A_ENTER    EQU 5     ; leave the welcome dialog
A_RESTART  EQU 7
A_POWER    EQU 8
A_CANCEL   EQU 9
A_REFRESH  EQU 10
A_PREV     EQU 11
A_NEXT     EQU 12
A_SAVE     EQU 13
A_SEL0     EQU 14    ; shut down radio buttons 14..15
A_SEL1     EQU 15
A_OK       EQU 17
A_FMOPEN   EQU 20    ; File Manager: open, copy, rename, delete
A_FMCOPY   EQU 21
A_FMREN    EQU 22
A_FMDEL    EQU 23

; ---- view states ----
V_START    EQU 0
V_DESKTOP  EQU 1
V_SHUTDOWN EQU 2
V_OFF      EQU 3
V_BOOT     EQU 4     ; the boot screen, before the welcome dialog
