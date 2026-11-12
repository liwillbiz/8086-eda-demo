; d6_rep_movsw_std — D6. STD; REP MOVSW with CX=4 copies 4 words downward.
; Source words at DS:0106h..0100h, destination at ES:0206h..0200h (DS = ES = 0200h).

%include "i86rom.inc"

start:
        INIT_RAM
        mov     ax, DATA_SEG
        mov     ds, ax
        mov     es, ax
        mov     word [0100h], 1111h
        mov     word [0102h], 2222h
        mov     word [0104h], 3333h
        mov     word [0106h], 4444h
        mov     word [0108h], 5555h     ; must not be copied
        mov     word [01FEh], 0EEEEh    ; destination guard below the block
        mov     si, 0106h
        mov     di, 0206h
        mov     cx, 4
        std
        rep     movsw
        cld
        REPORT  cx                      ; 0: brief: CX = 0
        REPORT  si                      ; 1: brief: SI = 0106h - 8 = 00FEh
        REPORT  di                      ; 2: brief: DI = 0206h - 8 = 01FEh
        REPORT  word [0200h]            ; 3..6: copied words
        REPORT  word [0202h]
        REPORT  word [0204h]
        REPORT  word [0206h]
        REPORT  word [01FEh]            ; 7: guard untouched
        hlt

        ROM_END
