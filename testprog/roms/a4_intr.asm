; a4_intr — A4. With IF=1 the tester raises INTR and answers the second INTA cycle with
; type 40h, so the vector comes from 00100h. STI shadows one instruction: MOV BX runs
; before the interrupt. The handler reports the stacked IP, CS and FLAGS, then IRETs.

%include "i86rom.inc"

start:
        INIT_RAM
        mov     word [0100h], intr_handler   ; vector 40h: IP then CS
        mov     word [0102h], ROM_SEG
        sti
        mov     bx, 1111h               ; in the STI shadow
resume:
        REPORT  bx                      ; 3: 1111h
        REPORT  resume                  ; 4: offset the handler must have returned to
        hlt

intr_handler:
        mov     bp, sp
        REPORT  word [bp]               ; 0: stacked IP
        REPORT  word [bp+2]             ; 1: stacked CS
        REPORT_FLAGS                    ; IF and TF clear inside the handler
        REPORT  word [bp+4]             ; 2: stacked FLAGS, IF set
        iret

        ROM_END
