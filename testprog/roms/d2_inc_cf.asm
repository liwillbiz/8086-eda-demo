; d2_inc_cf — D2. INC and DEC leave CF unchanged (brief §2), with CF set and clear.

%include "i86rom.inc"

start:
        INIT_RAM
        mov     ax, 0FFFFh
        stc
        inc     ax                      ; brief: CF still 1 (AX=0, ZF=1)
        REPORT_FLAGS
        mov     ax, 0
        stc
        dec     ax                      ; CF still 1 (AX=FFFFh)
        REPORT  ax
        REPORT_FLAGS
        mov     bx, 7FFFh
        clc
        inc     bx                      ; CF still 0, OF=1
        REPORT  bx
        REPORT_FLAGS
        hlt

        ROM_END
