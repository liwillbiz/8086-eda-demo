; a5_nmi_level — A5. The CPU halts; the tester then raises NMI and holds it high. NMI is
; edge triggered, so the type-2 handler runs once even though the level stays high
; through a delay loop and a second HLT.

%include "i86rom.inc"

start:
        INIT_RAM
        mov     word [0008h], nmi_handler    ; vector 2
        mov     word [000Ah], ROM_SEG
        xor     bx, bx
        hlt
after_hlt:
        REPORT  bx                      ; 1: entries after the edge (1)
        mov     cx, 40
.delay: dec     cx
        jnz     .delay
        REPORT  bx                      ; 2: still 1 with NMI held high
        REPORT  after_hlt               ; 3
        hlt

nmi_handler:
        inc     bx
        mov     bp, sp
        REPORT  word [bp]               ; 0: stacked IP, the instruction after HLT
        iret

        ROM_END
