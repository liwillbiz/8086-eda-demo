; d10_single_step — D10. POPF sets TF; each of the next three instructions traps (type 1).
; The handler counts entries, reports the stacked IP and its own FLAGS (TF clear), and on
; the third entry clears TF in the stacked FLAGS so IRET ends single-stepping.

%include "i86rom.inc"

start:
        INIT_RAM
        mov     word [0004h], trap_handler   ; vector 1
        mov     word [0006h], ROM_SEG
        xor     bx, bx
        pushf
        pop     ax
        or      ax, 0100h
        push    ax
        popf
step1:  nop
step2:  nop
step3:  nop
done:
        REPORT  bx                      ; 3: entries (3)
        REPORT  step2                   ; 4: offsets for the stacked-IP checks
        REPORT  step3                   ; 5
        REPORT  done                    ; 6
        REPORT_FLAGS                    ; 3: TF clear after the third IRET
        hlt

trap_handler:
        inc     bx
        push    bp
        mov     bp, sp
        REPORT  word [bp+2]             ; 0..2: stacked IP
        REPORT_FLAGS                    ; 0..2: TF clear in the handler
        cmp     bx, 3
        jne     .ret
        and     word [bp+6], 0FEFFh
.ret:   pop     bp
        iret

        ROM_END
