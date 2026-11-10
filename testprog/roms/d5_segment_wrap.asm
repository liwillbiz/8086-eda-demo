; d5_segment_wrap — D5. FFFFh:0010h = 100000h wraps to 00000h (20-bit bus, no A20).

%include "i86rom.inc"

start:
        INIT_RAM
        mov     byte [0000h], 11h       ; known value at 00000h first
        mov     ax, 0FFFFh
        mov     es, ax
        mov     byte [es:0010h], 0AAh   ; brief: the write appears at 00000h
        mov     al, [0000h]             ; DS = 0000h
        mov     ah, 0
        REPORT  ax
        hlt

        ROM_END
