; b1_checksum — B1 (and the B/C margin tests, the shmoo). Copies 32 words of this ROM to
; RAM, then folds the copy into a sum and a rotating XOR and divides one by the other, so
; read data, write data and the EU all reach the reported words. v1 subset only (design
; doc §7.3).

%include "i86rom.inc"

WORDS equ 32

start:
        INIT_RAM
        mov     ax, cs
        mov     ds, ax
        mov     ax, DATA_SEG
        mov     es, ax
        xor     si, si
        xor     di, di
        mov     cx, WORDS
        cld
        rep     movsw

        mov     ds, ax
        xor     si, si
        xor     bx, bx
        xor     dx, dx
        mov     cx, WORDS
.fold:  mov     ax, [si]
        add     si, 2
        add     bx, ax
        xor     dx, ax
        add     dx, dx                  ; rotate DX left by one (v1 subset has no ROL)
        adc     dx, 0
        dec     cx
        jnz     .fold

        REPORT  bx
        REPORT  dx
        mov     cx, dx
        or      cx, 1                   ; nonzero divisor; DX:AX = 0:BX cannot overflow
        xor     dx, dx
        mov     ax, bx
        div     cx
        REPORT  ax
        REPORT  dx
        hlt

        ROM_END
