; Shared conventions for the test ROMs (NASM, cpu 8086, flat binary).
;
; Layout: a 4 KB ROM at FF000h-FFFFFh (CS = FF00h). The reset entry FFFF0h holds a far
; jump to `start`. Unused bytes are HLT, so a runaway program stops.
; RAM the ROMs use: the vector table at 00000h, the stack at 00F00h-00FFFh
; (SS:SP = 0000:1000h), data from 02000h. A ROM must write RAM before reading it:
; the golden emulator's RAM reads as 00h, the tester's unwritten RAM floats.
;
; Results leave the CPU only through OUT, which the tester sees as I/O write cycles:
;   PORT_DATA  (E0h)  word values, compared exactly against the golden emulator
;   PORT_FLAGS (E2h)  FLAGS images, compared under the test's undefined-flag mask

cpu 8086
bits 16
org 0

ROM_SIZE   equ 1000h
ROM_SEG    equ 0FF00h
STACK_TOP  equ 1000h
DATA_SEG   equ 0200h

PORT_DATA  equ 0E0h
PORT_FLAGS equ 0E2h

; REPORT reg16|imm  — send a word to PORT_DATA (clobbers AX unless the operand is AX).
%macro REPORT 1
%ifnidni %1, ax
        mov     ax, %1
%endif
        out     PORT_DATA, ax
%endmacro

; REPORT_FLAGS — send the current FLAGS to PORT_FLAGS (clobbers AX, uses 2 stack bytes).
%macro REPORT_FLAGS 0
        pushf
        pop     ax
        out     PORT_FLAGS, ax
%endmacro

; DUMP_REGS — report AX BX CX DX SP BP SI DI DS ES SS, then FLAGS (clobbers AX).
%macro DUMP_REGS 0
        out     PORT_DATA, ax
        REPORT  bx
        REPORT  cx
        REPORT  dx
        REPORT  sp
        REPORT  bp
        REPORT  si
        REPORT  di
        REPORT  ds
        REPORT  es
        REPORT  ss
        REPORT_FLAGS
%endmacro

; INIT_RAM — SS:SP = 0000:STACK_TOP, DS = ES = 0000h.
%macro INIT_RAM 0
        xor     ax, ax
        mov     ss, ax
        mov     sp, STACK_TOP
        mov     ds, ax
        mov     es, ax
%endmacro

; ROM_END — pad with HLT and place the reset jump at FFFF0h.
%macro ROM_END 0
        times ROM_SIZE - 16 - ($ - $$) db 0F4h
        jmp     ROM_SEG:start
        times ROM_SIZE - ($ - $$) db 0F4h
%endmacro
