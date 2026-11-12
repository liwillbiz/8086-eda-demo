; d3_divide_error — D3. DIV BL with BL=0 raises type 0. The handler reports the stacked
; IP and CS, the IP of the instruction after the DIV (the 8086 stacks that), and the
; stacked FLAGS. DIV leaves every arithmetic flag undefined.

%include "i86rom.inc"

start:
        INIT_RAM
        mov     word [0000h], div_handler    ; vector 0: IP then CS
        mov     word [0002h], ROM_SEG
        mov     ax, 1
        mov     bl, 0
        div     bl
after_div:
        REPORT  0DEADh                  ; not reached: the handler halts
        hlt

div_handler:
        pop     bx                      ; stacked IP
        pop     cx                      ; stacked CS
        pop     dx                      ; stacked FLAGS
        REPORT  bx                      ; 0: stacked IP
        REPORT  cx                      ; 1: stacked CS
        REPORT  after_div               ; 2: offset of the instruction after DIV
        mov     ax, dx
        out     PORT_FLAGS, ax
        REPORT_FLAGS                    ; IF and TF clear inside the handler
        hlt

        ROM_END
