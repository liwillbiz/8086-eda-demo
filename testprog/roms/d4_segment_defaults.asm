; d4_segment_defaults — D4. [BP+2] defaults to SS, [BX+2] to DS; an ES: override wins.
; Each read hits a distinct physical address so the test can find its bus cycle:
;   SS=0300h BP=0010h -> 03012h   DS=0200h BX=0010h -> 02012h   ES=0400h -> 04012h

%include "i86rom.inc"

start:
        INIT_RAM
        mov     ax, 0300h
        mov     ss, ax
        mov     sp, 0F00h
        mov     ax, DATA_SEG
        mov     ds, ax
        mov     ax, 0400h
        mov     es, ax
        mov     bp, 0010h
        mov     bx, 0010h
        mov     word [bp+2], 0A55Ah     ; SS:0012h = 03012h
        mov     word [bx+2], 0B66Bh     ; DS:0012h = 02012h
        mov     word [es:bx+2], 0C77Ch  ; ES:0012h = 04012h

        mov     ax, [bp+2]              ; read 03012h, S4 S3 = 01 (SS)
        REPORT  ax
        mov     ax, [bx+2]              ; read 02012h, S4 S3 = 11 (DS)
        REPORT  ax
        mov     ax, [es:bx+2]           ; read 04012h, S4 S3 = 00 (ES)
        REPORT  ax
        hlt

        ROM_END
