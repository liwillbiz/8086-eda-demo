; d1_add_flags — D1. ADD flags, brief case first, then byte carry and word overflow.
; Each case reports the result (PORT_DATA) and then FLAGS (PORT_FLAGS).

%include "i86rom.inc"

%macro ADD_CASE 3                       ; dst, a, b
        mov     %1, %2
        add     %1, %3
        pushf
        pop     dx
        REPORT  ax
        mov     ax, dx
        out     PORT_FLAGS, ax
%endmacro

start:
        INIT_RAM
        mov     ax, 0
        ADD_CASE al, 7Fh, 1             ; brief: AL=80h OF=1 SF=1 ZF=0 AF=1 PF=0 CF=0
        mov     ax, 0
        ADD_CASE al, 0FFh, 1            ; AL=00h CF=1 ZF=1 AF=1 PF=1
        ADD_CASE ax, 7FFFh, 1           ; AX=8000h OF=1 SF=1 AF=1
        hlt

        ROM_END
