; d7_rep_intr — D7. The tester raises INTR (type 41h) part way through REP MOVSB. The 8086
; stacks the IP of the REP prefix, so after IRET the copy resumes with CX, SI and DI as the
; handler left them. The handler reports CX + (DI - 3000h), which is 64 wherever the copy
; was interrupted.

%include "i86rom.inc"

COUNT   equ 64

start:
        INIT_RAM
        mov     word [0104h], intr_handler   ; vector 41h
        mov     word [0106h], ROM_SEG
        mov     di, 2000h
        mov     cx, COUNT
        mov     al, 1
.fill:  mov     [di], al
        add     al, 3
        inc     di
        dec     cx
        jnz     .fill
        cld
        sti
        mov     si, 2000h
        mov     di, 3000h
        mov     cx, COUNT
rep_at: rep     movsb
        REPORT  cx                      ; 2: 0
        REPORT  si                      ; 3: 2040h
        REPORT  di                      ; 4: 3040h
        REPORT  word [3000h]            ; 5..7: copied bytes, pairwise
        REPORT  word [301Eh]
        REPORT  word [303Eh]
        REPORT  rep_at                  ; 8
        hlt

intr_handler:
        mov     bp, sp
        REPORT  word [bp]               ; 0: stacked IP, the REP prefix
        mov     ax, di
        sub     ax, 3000h
        add     ax, cx
        REPORT  ax                      ; 1: CX + bytes already copied
        iret

        ROM_END
