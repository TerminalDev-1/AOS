; DATA.INC - state, button tables, strings and bitmaps.

fontoff dw 0
fontseg dw 0
; Stable telemetry block read by the emulator tests: keep this order.
signature db 'V4GUIstate'
viewstate db V_START                   ; V_START / V_DESKTOP / V_SHUTDOWN / V_OFF
opened db 0                            ; window bitset: bit n = window n+1
active db 1                            ; top window, 1..3
dirty db 1                             ; whole scene needs repainting
mouseok db 0
mx dw 320
my dw 240

; ---- screen: 640 x 480 unless VIDEO.INC found a wider mode ----
wide db 0                              ; the QEMU wide mode is on
scrw dw 640                            ; width in pixels
maxx dw 639
rowb dw 80                             ; bytes in a row of one plane
lastcol dw 79
rowskip dw 80-WIN_W/8                  ; from a window row to the next
backofs dw (WIN_H-1)*80+(WIN_W/8-1)    ; last byte of a window, from its first
wxmax dw 640-WIN_W                     ; rightmost window position
dlgx dw 0                              ; shift that centres the dialogs
guix dw (640-WIN_W)/2                  ; x of a program's window
clockx dw 548                          ; x of the tray clock

; ---- input ----
buttons db 0
lastbuttons db 0
drag db 0
dragdx dw 0
dragdy dw 0
curdirty db 0                          ; only the pointer moved
winmoved db 0                          ; dragged window needs a blit
hwx dw 0
hwy dw 0
hidden db 0                            ; minimized windows (bitset like opened)
menuopen db 0                          ; Start menu showing
menuhot db 0ffh                        ; highlighted menu item, FF = none
iconsel db 0                           ; selected desktop icon, 0 = none
lastclk dw 0
shutsel db 0                           ; Shut Down choice 0..1

; ---- windows (x always a multiple of 8) ----
wx dw 96,128,160,192
wy dw 20,52,84,116
wox dw 0
woy dw 0
cw_w dw 0
cw_h dw 0
tbact db 0
tbmin db 0
tbtxt db 0
curwin dw 0
oldx dw 0                              ; where the dragged window is still drawn
oldy dw 0
nwx dw 0
nwy dw 0
rox0 dw 0
roy0 dw 0
rox1 dw 0
roy1 dw 0
rbx0 dw 0
rby0 dw 0
rbx1 dw 0
rby1 dw 0
rxl dw 0
rxr dw 0
ryl dw 0
ryh dw 0

; ---- renderer ----
rx dw 0
ry dw 0
rw dw 0
rh dw 0
ink db 0
tx dw 0
ty dw 0
fg db 0
ctrx dw 0
bsc dw 1
clx0 dw 0                              ; clip box in pixels, upper bounds exclusive
cly0 dw 0
clx1 dw 640
cly1 dw 480
clipped db 0                           ; 1 while repainting a sub-area
rcx0 dw 0
rcx1 dw 0
rrows dw 0
rrb dw 0
rcnt dw 0
rlm db 0
rrm db 0
tpx dw 0
tr0 dw 0
trn dw 0
trow dw 0
tclb0 dw 0
tclb1 dw 0
glyph dw 0
sft db 0
ok0 db 0
ok1 db 0
bgl dw 0
bcx dw 0
bcy dw 0
bmk db 0
curx dw 0                              ; pointer position as last drawn
cury dw 0
ccol dw 0
csft db 0
savedaddr dw 0
savedcol dw 0
under db 192 dup (0)                   ; 4 planes x 16 rows x 3 bytes
rtx dw 0                               ; rotated text origin
rty dw 0

; ---- widgets ----
orgx dw 0
orgy dw 0
bbx dw 0
bby dw 0
bbw dw 0
bbh dw 0
bst db 0
bact db 0
blbl dw 0
icx dw 0
icy dw 0
bvx dw 0
bvy dw 0
bvw dw 0
bvh dw 0
bvm db 0
bins dw 2
bc1 db 0
bc2 db 0
bc3 db 0
bc4 db 0
bcf db 0
gx dw 0
gy dw 0
gw dw 0
gh dw 0
gc1 db 0
gc2 db 0
gc3 db 0
gca db 0
gcb db 0
gseg dw 0
gxx dw 0
gwid dw 0
xgx dw 0
xgy dw 0
pi dw 0
slot dw 0

; ---- storage ----
files db 224*14 dup (0)                ; attribute byte + 13-byte name
filecount dw 0
filepage dw 0
fk dw 0
fcnt dw 0
fxx dw 0
fyy dw 0
frow dw 0
dta db 43 dup(0)
filemask db '*.*',0
note db 241 dup(0)
notelen dw 0
nline dw 0
notefile db 'NOTES.TXT',0
status dw OFFSET hintnotes
linebuf db 64 dup (0)
clockbuf db '12:00 AM',0,0,0
lastclock dw 0ffffh
clockdirty db 0

; ---- FAT12 volume (see FS.INC) ----
fs_drive db 0
fs_op db 2
fs_f db 0
fs_nfat db 2
fs_spt dw 18
fs_heads dw 2
fs_rsvd dw 1
fs_fatsz dw 9
fs_rootent dw 224
fs_rootsecs dw 14
fs_rootlba dw 19
fs_datalba dw 33
fs_nclus dw 2847
fs_cnt dw 0
fs_ptr dw 0
fs_ent dw 0
fs_first dw 0
fs_prev dw 0
fs_n dw 0
fs_size dw 0
fs_cur dw 0
fs_src dw 0
fsname db 11 dup (' ')
fsname2 db 11 dup (' ')

; ---- WinKit terminal (see TERM.INC) ----
termsig db 'WKTERMBUF'                 ; tests find the text screen by this
tbuf db 16*52 dup (0)                  ; 16 rows x (50 chars + spare + NUL)
term_col dw 0
term_row dw 0
cmdbuf db 64 dup (0)
cmdlen dw 0
cmdp dw 0
argp dw 0
arg1 db 24 dup (0)
arg2 db 24 dup (0)
fnbuf db 16 dup (0)
dirn dw 0
kss dw 0
ksp dw 0
termdirty db 0
guiopen db 0                           ; a program has its window up
guititle db 42 dup (0)
storefile db 'WKSTORE.WKX',0
chainfile db 16 dup (0)                ; program to run when this one exits
gewait db 0                            ; 0 wait for an event, 1/2 one tick only
keydirty db 0                          ; dirty as it was before the key
subopen db 0                           ; Programs list showing
subhot db 0ffh                         ; highlighted program, FF = none
progcount dw 0                         ; .WKX programs found by prog_scan
progptr dw SUB_MAX dup (0)             ; their names in files[]
deskcount dw 0                         ; programs with a desktop shortcut
deskptr dw DESK_MAX dup (0)
dcolx dw 0                             ; x of a desktop icon's column
filesaved db 0                         ; a program saved a file

cmdtab dw OFFSET c_help,OFFSET cmd_help
       dw OFFSET c_dir,OFFSET cmd_dir
       dw OFFSET c_type,OFFSET cmd_type
       dw OFFSET c_echo,OFFSET cmd_echo
       dw OFFSET c_copy,OFFSET cmd_copy
       dw OFFSET c_ren,OFFSET cmd_ren
       dw OFFSET c_del,OFFSET cmd_del
       dw OFFSET c_cls,OFFSET cmd_cls
       dw OFFSET c_ver,OFFSET cmd_ver
       dw OFFSET c_time,OFFSET cmd_time
       dw OFFSET c_mem,OFFSET cmd_mem
       dw OFFSET c_reboot,OFFSET cmd_reboot
       dw OFFSET c_exit,OFFSET cmd_exit
       dw 0,0
c_help db 'help',0
c_dir db 'dir',0
c_type db 'type',0
c_echo db 'echo',0
c_copy db 'copy',0
c_ren db 'ren',0
c_del db 'del',0
c_cls db 'cls',0
c_ver db 'ver',0
c_time db 'time',0
c_mem db 'mem',0
c_reboot db 'reboot',0
c_exit db 'exit',0
t_prompt db 'winkit> ',0
t_banner db 'WinKit-preview   type help for the commands',0
t_ver db 'WinKit-preview - standalone 16-bit system',0
t_mem db ' KB conventional memory',0
t_files db ' file(s), ',0
t_free db ' KB free',0
t_wkx db '.WKX',0
t_nofile db 'File not found.',0
t_badcmd db 'Bad command or program name.',0
t_diskerr db 'Disk error or disk full.',0
t_renfail db 'Cannot rename (missing, or name in use).',0
t_usage_type db 'Usage: type FILE',0
t_usage_echo db 'Usage: echo TEXT > FILE',0
t_usage_copy db 'Usage: copy FROM TO',0
t_usage_del db 'Usage: del FILE',0
t_usage_ren db 'Usage: ren OLD NEW',0
t_help db 'WinKit commands:',0
       db '  dir           list the files',0
       db '  type FILE     show a text file',0
       db '  echo TEXT     print text; add > FILE to save',0
       db '  copy A B      copy a file',0
       db '  ren A B       rename a file',0
       db '  del FILE      delete a file',0
       db '  cls ver time mem reboot exit',0
       db '  NAME          run NAME.WKX',0
       db 0

; ---- button tables: x,y,w,h, style, action, label ----
welcome_tab:
    dw 284,306,96,28
    db S_DEFAULT,A_ENTER
    dw OFFSET lbl_ok
    dw 388,306,112,28
    db S_PUSH,A_SHUTDOWN
    dw OFFSET lbl_shutdn
    dw 0ffffh

shut_tab:
    dw 204,188,280,24
    db S_RADIO,A_SEL0
    dw OFFSET lbl_r0
    dw 204,214,280,24
    db S_RADIO,A_SEL1
    dw OFFSET lbl_r1
    dw 212,318,88,28
    db S_DEFAULT,A_OK
    dw OFFSET lbl_ok
    dw 308,318,88,28
    db S_PUSH,A_CANCEL
    dw OFFSET lbl_cancel
    dw 0ffffh

; Window-relative tables.
files_tab:
    dw 10,25,84,24
    db S_PUSH,A_REFRESH
    dw OFFSET lbl_refresh
    dw 100,25,64,24
    db S_PUSH,A_PREV
    dw OFFSET lbl_prev
    dw 170,25,64,24
    db S_PUSH,A_NEXT
    dw OFFSET lbl_next
    dw 0ffffh

notes_tab:
    dw 10,25,88,24
    db S_PUSH,A_SAVE
    dw OFFSET lbl_save
    dw 0ffffh

titleptr dw OFFSET ttl_files,OFFSET ttl_notes,OFFSET ttl_system,OFFSET ttl_term
iconlbl dw OFFSET lbl_files,OFFSET lbl_notes,OFFSET lbl_system,OFFSET lbl_term
        dw OFFSET lbl_store

; ---- palette: 16 attribute registers + overscan, then 16 DAC triplets ----
attrmap db 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,0
dacrgb db  0, 0, 0      ; 0  black
       db  0, 0,32      ; 1  navy
       db  4,33,52      ; 2  sky
       db  0,32,32      ; 3  teal
       db 48,48,48      ; 4  face
       db 32,32,32      ; 5  shadow
       db 63,63,63      ; 6  white
       db 56,56,56      ; 7  light
       db 45,45,45      ; 8  inactive title
       db 63, 0, 0      ; 9  red
       db 63,63, 0      ; 10 yellow
       db  0,32, 0      ; 11 green
       db  2,16,42      ; 12 title midpoint
       db  0, 0,63      ; 13 blue
       db 63,32, 0      ; 14 orange
       db  0,63,63      ; 15 cyan

; ---- strings ----
lbl_files db 'Files',0
lbl_notes db 'Notes',0
lbl_system db 'System',0
lbl_start db 'Start',0
lbl_ok db 'OK',0
lbl_cancel db 'Cancel',0
lbl_shutdn db 'Shut Down...',0
lbl_refresh db 'Refresh',0
lbl_prev db '< Prev',0
lbl_next db 'Next >',0
lbl_save db 'Save  F4',0
lbl_r0 db 'Shut down',0
lbl_r1 db 'Restart',0
lbl_term db 'WinKit',0
lbl_store db 'Store',0
mn_files db 'Files',0
mn_notes db 'Notes',0
mn_system db 'System',0
mn_term db 'WinKit',0
mn_store db 'Store',0
mn_progs db 'Programs',0
s_arrow db '>',0
s_noprogs db '(none)',0
mn_shut db 'Shut Down...',0
banner db 'WinKit-preview',0
logo db 'WK',0
menuptr dw OFFSET mn_files,OFFSET mn_notes,OFFSET mn_system,OFFSET mn_term
        dw OFFSET mn_store,OFFSET mn_progs,OFFSET mn_shut

ttl_files db 'Files - boot disk',0
ttl_notes db 'Notes - NOTES.TXT',0
ttl_system db 'System Properties',0
ttl_welcome db 'Welcome to WinKit-preview',0
ttl_shut db 'Shut Down',0
ttl_term db 'WinKit',0

w_title db 'WinKit-preview',0
w_sub db 'A standalone graphical operating system',0
w_body db 'Files, notes and the WinKit terminal.',0
w_hint db 'Press Enter to open the desktop.',0
sd_text db 'What do you want the computer to do?',0
sd_hint db 'R Restart   P Power off   Esc Cancel',0

of_1 db 'It',27h,'s now safe to',0
of_2 db 'turn off your',0
of_3 db 'computer.',0
of_hint db 'Press Enter to restart.',0

s_page db 'Page ',0
s_objects db ' object(s)',0
s_nofiles db 'No files found.',0
s_limit db '/240',0
s_root db 'Boot disk, root folder',0
hintnotes db 'Type to edit.  F4 saves.',0
savedtext db 'Saved to NOTES.TXT',0
errortext db 'Cannot save - disk full or protected',0
n_sub db 'Plain text, up to 240 characters',0

sy_tab db 'General',0
sy_h1 db 'System:',0
sy_a1 db 'WinKit-preview desktop',0
sy_a2 db 'Standalone: boots without a DOS kernel',0
sy_h2 db 'Computer:',0
sy_b1 db 'VGA 640 x 480, 16 colours',0
sy_b2 db 'PS/2 mouse (BIOS) and keyboard',0
sy_b3 db 'FAT12 floppy, BIOS disk services',0
sy_f1 db 'Written in 16-bit assembly.',0
sy_f2 db 'WinKit terminal: Start menu or F6.',0

; ---- pointer bitmaps: 16 rows x 16 bits ----
cursorblack db 80h,0,0c0h,0,0e0h,0,0f0h,0,0f8h,0,0fch,0,0feh,0,0ffh,0
            db 0ffh,80h,0ffh,0c0h,0fch,0,0deh,0,8fh,0,7,80h,3,80h,1,0
cursorwhite db 0,0,0,0,40h,0,60h,0,70h,0,78h,0,7ch,0,7eh,0
            db 7fh,0,78h,0,68h,0,44h,0,6,0,3,0,1,0,0,0
