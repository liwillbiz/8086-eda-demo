; d11_invalid_opcode — D11. 0Fh (POP CS) and D6h (SALC) execute on the 8086; neither
; raises the type-6 "invalid opcode" trap of later chips. A type-6 handler reports
; 0666h if it is ever entered.

%include "i86rom.inc"

start:
        INIT_RAM
        mov     word [6*4], trap6
        mov     word [6*4+2], ROM_SEG
        push    cs
        db      0Fh                     ; POP CS: CS unchanged, execution continues
        REPORT  cs                      ; 0: FF00h
        stc
        db      0D6h                    ; SALC: AL = FFh when CF = 1
        mov     ah, 0
        REPORT  ax                      ; 1: 00FFh
        clc
        db      0D6h                    ; AL = 00h when CF = 0
        mov     ah, 0
        REPORT  ax                      ; 2: 0000h
        REPORT  sp                      ; 3: SP back at STACK_TOP
        hlt

trap6:
        REPORT  0666h
        hlt

        ROM_END
