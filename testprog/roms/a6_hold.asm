; a6_hold — A6. A memory-heavy loop the tester interrupts with HOLD. While HLDA is high
; the CPU must not run bus cycles; afterwards the loop finishes with the same results.

%include "i86rom.inc"

start:
        INIT_RAM
        mov     di, 2000h
        mov     cx, 32
        mov     ax, 1234h
.fill:  mov     [di], ax
        add     ax, 0F0Fh
        add     di, 2
        dec     cx
        jnz     .fill
        mov     si, 2000h
        xor     bx, bx
        mov     cx, 32
.sum:   add     bx, [si]
        add     si, 2
        dec     cx
        jnz     .sum
        REPORT  bx                      ; 0: sum of the 32 words
        REPORT  word [2000h]            ; 1
        REPORT  word [203Eh]            ; 2
        hlt

        ROM_END
