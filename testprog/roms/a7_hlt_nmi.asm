; a7_hlt_nmi — A7. HLT puts the bus in the halt state; a pulse on NMI then brings the CPU
; out through the type-2 handler, which returns to the instruction after HLT.

%include "i86rom.inc"

start:
        INIT_RAM
        mov     word [0008h], nmi_handler    ; vector 2
        mov     word [000Ah], ROM_SEG
        REPORT  1                       ; 0: before HLT
        hlt
after_hlt:
        REPORT  3                       ; 3: after the handler
        REPORT  after_hlt               ; 4
        hlt

nmi_handler:
        REPORT  2                       ; 1: in the handler
        mov     bp, sp
        REPORT  word [bp]               ; 2: stacked IP
        iret

        ROM_END
