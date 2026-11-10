; d8_shift_unmasked — D8. SHL AX,CL with CL=33 shifts 33 times (no 5-bit mask): AX = 0.
; A 286 or later would shift by 1. AF, and OF for counts other than 1, are undefined.

%include "i86rom.inc"

start:
        INIT_RAM
        mov     ax, 0FFFFh
        mov     cl, 33
        shl     ax, cl                  ; brief: AX = 0
        pushf
        pop     dx
        REPORT  ax
        mov     ax, dx
        out     PORT_FLAGS, ax
        mov     ax, 8001h
        mov     cl, 17
        shr     ax, cl                  ; 17 > 16: AX = 0
        REPORT  ax
        mov     ax, 1234h
        mov     cl, 1
        shl     ax, cl                  ; count 1, for reference: AX = 2468h
        REPORT  ax
        REPORT  cx
        hlt

        ROM_END
