; boot — A1 (reset vector) and A2 (reset register state).
; Reports the segment registers and FLAGS exactly as RESET left them, before any
; instruction that could change them.

%include "i86rom.inc"

start:
        REPORT  ds                      ; 0: DS
        REPORT  es                      ; 1: ES
        REPORT  ss                      ; 2: SS
        REPORT  cs                      ; 3: CS (FF00h after the reset far jump)
        mov     sp, STACK_TOP           ; SS is 0000h after RESET
        REPORT_FLAGS                    ; FLAGS at reset (bits 15-12 read as 1)
        hlt

        ROM_END
