; d9_flags_identity — D9. PUSHF after POPF of 0000h: bits 15-12 read as 1 on an 8086.

%include "i86rom.inc"

start:
        INIT_RAM
        xor     ax, ax
        push    ax
        popf
        pushf
        pop     ax
        REPORT  ax                      ; brief: AX & F000h = F000h
        mov     ax, 0FEFFh              ; every bit but TF (a trap would need a vector)
        push    ax
        popf
        pushf
        pop     ax
        cli
        REPORT  ax                      ; brief: FEFFh & FFD5h | F002h = FED7h
        hlt

        ROM_END
