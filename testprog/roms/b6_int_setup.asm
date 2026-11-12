; b6_int_setup — B6. The CPU halts with IF=1; the tester then raises NMI or INTR (type 42h)
; at a set time before a CLK rising edge. Each handler reports its type and returns.

%include "i86rom.inc"

start:
        INIT_RAM
        mov     word [0008h], nmi_handler    ; vector 2
        mov     word [000Ah], ROM_SEG
        mov     word [0108h], intr_handler   ; vector 42h
        mov     word [010Ah], ROM_SEG
        sti
        hlt
        REPORT  0B6h                    ; 1
        hlt

nmi_handler:
        REPORT  2                       ; 0
        iret

intr_handler:
        REPORT  42h                     ; 0
        iret

        ROM_END
