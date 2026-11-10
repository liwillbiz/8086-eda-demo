; a3_byte_lanes — A3. Word and byte writes and reads at even and odd addresses.
; The test checks BHE#/A0 on each data cycle against the spec decode table and that an
; odd-address word takes two bus cycles. Data addresses (DS = 0200h):
;   02100h even word, 02111h odd word, 02120h even byte, 02131h odd byte

%include "i86rom.inc"

start:
        INIT_RAM
        mov     ax, DATA_SEG
        mov     ds, ax

        mov     word [0100h], 1234h     ; even word write: 1 cycle, BHE#=0 A0=0
        mov     word [0111h], 5678h     ; odd word write:  2 cycles
        mov     byte [0120h], 9Ah       ; even byte write: BHE#=1 A0=0
        mov     byte [0131h], 0BCh      ; odd byte write:  BHE#=0 A0=1

        mov     ax, [0100h]             ; even word read
        REPORT  ax
        mov     ax, [0111h]             ; odd word read
        REPORT  ax
        mov     al, [0120h]             ; even byte read
        mov     ah, [0131h]             ; odd byte read
        REPORT  ax
        hlt

        ROM_END
