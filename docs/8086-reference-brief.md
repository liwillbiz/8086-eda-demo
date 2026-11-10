# Intel 8086 Reference Brief — for a Team Chip-Testing Project

Purpose: a single skimmable reference that a 3–5 person team can share when planning and dividing test work on the 8086 (HMOS, 40-pin DIP).

## Sources and how to read the citations

| Tag | Source | Access status |
|---|---|---|
| **[CS p.N]** | Jishan Shaikh, *8086 Microprocessor Cheatsheet* v2 (May 2022), 17 pp. — <https://jishanshaikh4.github.io/8086-cheatsheet/main.pdf> | **Read in full.** Retrieved from the identical file in the GitHub repo (`docs/main.pdf` plus `latex-source/main.tex`); page numbers are from that PDF. |
| **[DS §Section]** | Intel, *8086 16-Bit HMOS Microprocessor 8086/8086-2/8086-1*, order no. 231455-006 — <https://datasheets.chipdb.org/Intel/x86/808x/datashts/8086/231455-006.pdf> | **Not retrieved.** This environment's network allowlist blocks `datasheets.chipdb.org`, and no mirror was reachable. Values tagged [DS] are recalled from the standard Intel datasheet and cited by **section/table name** rather than page. |

> **Verify before relying on any number for pass/fail limits:** open the datasheet and check every [DS] value against its *Pin Description*, *D.C. Characteristics*, and *A.C. Characteristics* tables. Values marked **(verify)** are the ones I'm least sure of. Speed-grade columns differ (8086 = 5 MHz, 8086-2 = 8 MHz, 8086-1 = 10 MHz).

**Cheatsheet errata (corrected below):** the cheatsheet calls DF the "Destination" flag (it is the **Direction** flag) [CS p.6]. It says the CS segment is "64-bit" (it is 64 KB) [CS p.3]. It gives the interrupt vector table as 0000h–02FFh, but 256 vectors × 4 bytes = **00000h–003FFh** [CS p.10; DS §Memory Organization]. It says the BIU calculates operand addresses; in practice the EU computes the 16-bit effective address and the BIU adds the shifted segment base to form the 20-bit physical address [CS p.1].

---

## 1. Architecture

### 1.1 At a glance
| Item | Value | Ref |
|---|---|---|
| Data width | 16-bit ALU and external data bus | CS p.1; DS §Features |
| Address space | 20-bit → 1 MB memory; 64 K I/O ports (16-bit I/O address) | CS p.1 |
| Bus | Multiplexed AD15–AD0, A19/S6–A16/S3 | CS p.1; DS §Pin Description |
| Registers | 14 × 16-bit | CS p.1, p.3 |
| Prefetch queue | 6 bytes (BIU fetches when ≥2 bytes free) | CS p.1–2 |
| Clock | Single phase, 33% duty cycle (from 8284A) | CS p.1; DS §A.C. Characteristics |
| Supply / package | +5 V; 40-pin DIP (CERDIP / plastic) | CS p.1; DS §Features |
| Speed grades | 8086: 5 MHz · 8086-2: 8 MHz · 8086-1: 10 MHz | DS §Features |

### 1.2 BIU vs EU
| Unit | Contains | Does | Ref |
|---|---|---|---|
| **BIU** (Bus Interface Unit) | Segment regs CS/DS/SS/ES, IP, address adder (Σ), 6-byte queue, bus control | All external bus cycles: prefetch, operand read/write, physical-address formation | CS p.1–2 |
| **EU** (Execution Unit) | ALU, general/pointer/index regs, flags, decoder, control | Decodes from the queue and executes; requests operand cycles from the BIU | CS p.2–3 |

The two units overlap fetch and execute (pipelining). Any jump, call, return, or interrupt **flushes the queue** and the BIU refetches from the new address [CS p.3]. On the pins, QS1/QS0 reports this as "empty the queue" (max mode) [DS §Pin Description].

### 1.3 Registers
| Group | Registers | Notes | Ref |
|---|---|---|---|
| Data | AX (AH/AL), BX (BH/BL), CX (CH/CL), DX (DH/DL) | AX: accumulator, I/O, MUL/DIV. BX: base pointer. CX: count for LOOP/REP/shifts (CL). DX: I/O port, high word for MUL/DIV. | CS p.4–5 |
| Pointer/Index | SP, BP, SI, DI | BP defaults to SS. SI/DI are the string source/destination. | CS p.5 |
| Segment | CS, DS, SS, ES | CS changes only via far JMP/CALL/RET/INT (POP CS is not a valid 8086 practice) | CS p.3–4 |
| Control | IP, FLAGS | 9 of 16 flag bits used | CS p.5–6 |

### 1.4 Flags (FLAGS register)
| Bit | 15–12 | 11 | 10 | 9 | 8 | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Flag | — (read as 1 on 8086) | OF | DF | IF | TF | SF | ZF | — | AF | — | PF | — (1) | CF |

| Flag | Meaning | Ref |
|---|---|---|
| CF | Carry/borrow out of MSB | CS p.6 |
| PF | Even parity of the **low byte** of the result | CS p.6 |
| AF | Carry/borrow out of bit 3 (BCD adjust) | CS p.6 |
| ZF / SF | Result zero / MSB of result | CS p.6 |
| OF | Signed overflow | CS p.6 |
| TF (control) | Trap → type-1 interrupt after each instruction | CS p.6, p.9 |
| IF (control) | Enables INTR (STI/CLI) | CS p.6, p.8 |
| DF (control) | **Direction**: 1 = string ops auto-decrement SI/DI | CS p.6 (mislabelled) |

On a real 8086, PUSHF stores bits 15–12 as 1s. This is a classic way to tell an 8086/8088 apart from a 286 or later chip, and it makes a useful silicon-identity test. This comes from general 8086 knowledge, not from either source.

### 1.5 Segmentation and memory map
- **Physical address = segment × 16 + offset**, e.g. `1234h:0010h → 12350h` [CS p.10]. Each segment is 64 KB, and segments may overlap.
- Default segment selection [DS §Memory Organization, "Segment register selection" table; CS p.3–4]:

| Reference type | Default segment | Override allowed? | Offset from |
|---|---|---|---|
| Instruction fetch | CS | No | IP |
| Stack (PUSH/POP/CALL/RET) | SS | No | SP |
| BP-based data | SS | Yes (CS/DS/ES) | effective address |
| Other data | DS | Yes (CS/ES/SS) | effective address |
| String source | DS | Yes | SI |
| String destination | ES | **No** | DI |

- **Reserved areas** [DS §Memory Organization; CS p.10]: `00000h–003FFh` holds the interrupt vector table (256 × CS:IP, IP stored first). `FFFF0h–FFFFFh` holds the reset/boot code; execution starts at FFFF0h.
- **Byte order:** little-endian. Far pointers are stored as offset then segment [DS]. Note that [CS p.10] states segment first, which is wrong for vectors and LDS/LES; the datasheet layout governs.
- **Odd-address words** take two bus cycles; even-address words take one [CS p.10]. This is directly observable with BHE#/A0 (§3.3).

### 1.6 Addressing modes
The datasheet claims "24 operand addressing modes" [DS §Features]. The cheatsheet groups them as follows [CS p.6–7]:

| Mode | Example | Effective address |
|---|---|---|
| Implied | `CLC`, `AAA` | — |
| Register | `MOV AX,BX` | — |
| Immediate | `MOV AX,1234h` | — |
| Direct | `MOV AX,[1234h]` | disp16 |
| Register indirect | `MOV AX,[SI]` | BX / SI / DI (BP needs a displacement) |
| Based | `MOV AX,[BX+4]`, `[BP+4]` | BX/BP + disp8/16 |
| Indexed | `MOV AX,[SI+4]` | SI/DI + disp |
| Based-indexed | `MOV AX,[BX+SI]` | BX/BP + SI/DI |
| Based-indexed + disp | `MOV AX,[BP+DI+8]` | BX/BP + SI/DI + disp |

The 24 modes come from the ModR/M byte: 8 r/m combinations × 3 displacement forms (none, d8, d16), with `mod=00 r/m=110` meaning direct addressing. Any r/m that uses BP defaults to SS.

---

## 2. Instruction set highlights
Groups follow [CS p.10–12, Figs 9–15 on pp.11, 15–17]. The datasheet's *Instruction Set Summary* table gives the binary encodings [DS §Instruction Set Summary]. Clock counts are **not** in the datasheet; take them from the 8086 Family User's Manual.

| Group | Key instructions | Test-relevant notes |
|---|---|---|
| Data transfer | MOV, XCHG, LEA, LDS/LES, PUSH/POP, PUSHF/POPF, XLAT, IN/OUT, LAHF/SAHF | IN/OUT use a fixed 8-bit port or DX; there is no MOV between two memory operands |
| Arithmetic | ADD/ADC/SUB/SBB/INC/DEC/NEG/CMP, MUL/IMUL, DIV/IDIV, CBW/CWD, DAA/DAS/AAA/AAS/AAM/AAD | INC/DEC leave CF unchanged. A DIV quotient overflow or divide by zero raises type 0. |
| Logic / shift | AND/OR/XOR/NOT/TEST, SHL/SAL/SHR/SAR, ROL/ROR/RCL/RCR | Shift count is 1 or CL, **not masked** on the 8086 (CL=255 is legal and slow) |
| Strings | MOVS, CMPS, SCAS, LODS, STOS (B/W) + REP/REPE/REPNE | Direction set by DF; count in CX; a string op can be interrupted mid-REP |
| Control transfer | JMP/CALL/RET (near/far), Jcc (±128), JCXZ, LOOP/LOOPE/LOOPNE | Every taken branch flushes the queue |
| Interrupt | INT n, INT 3 (1-byte CCh), INTO, IRET | §5 |
| Processor control | CLC/STC/CMC, CLD/STD, CLI/STI, HLT, WAIT, ESC, LOCK, NOP | WAIT samples TEST#; ESC hands off to the 8087; LOCK asserts LOCK# |
| Prefixes | Segment override, REP*, LOCK | Known 8086 quirk: after an interrupt in a multi-prefix REP string op, only the last prefix is honored on resume (general knowledge; verify) |

---

## 3. Pin functions and min/max mode
Source: [DS §Pin Description table, §Pin Configuration figure]; [CS p.4 Fig 3, p.6]. A `#` suffix means active low.

### 3.1 Common pins
| Pin(s) | Name | Dir | Function |
|---|---|---|---|
| 1, 20 | GND | — | Ground (both must be connected) |
| 40 | VCC | — | +5 V |
| 2–16, 39 | AD14–AD0, AD15 | I/O, 3-state | Address in T1, data in T2–T4 (pin 2 = AD14 … pin 16 = AD0; pin 39 = AD15) |
| 35–38 | A19/S6 … A16/S3 | O, 3-state | Address in T1, then status: S3/S4 = segment in use; S5 = IF; S6 = 0 |
| 34 | BHE#/S7 | O, 3-state | Bus high enable (enables D15–D8) |
| 32 | RD# | O, 3-state | Read strobe |
| 22 | READY | I | Synchronized via the 8284A; low inserts Tw |
| 23 | TEST# | I | Sampled by WAIT; stays in WAIT while high |
| 18 | INTR | I | Maskable interrupt, level-sensitive, sampled at the end of each instruction |
| 17 | NMI | I | Non-maskable, rising-edge, type 2 |
| 21 | RESET | I | Active high; must be held ≥ 4 CLK cycles (≥ 50 µs at power-up) |
| 19 | CLK | I | 33% duty-cycle clock |
| 33 | MN/MX# | I | Strap: VCC = minimum mode, GND = maximum mode |

### 3.2 Mode-dependent pins (24–31)
| Pin | Minimum mode (MN/MX# = 1) | Maximum mode (MN/MX# = 0) |
|---|---|---|
| 31 | HOLD (input) | RQ#/GT0# (bidirectional, higher priority) |
| 30 | HLDA (output) | RQ#/GT1# |
| 29 | WR# | LOCK# |
| 28 | M/IO# (1 = memory on the 8086) | S2# |
| 27 | DT/R# | S1# |
| 26 | DEN# | S0# |
| 25 | ALE | QS0 |
| 24 | INTA# | QS1 |

In **minimum mode** the CPU generates its own bus control and supports a single-processor system [CS p.6]. In **maximum mode** an 8288 bus controller decodes S2#–S0# into MRDC#/MWTC#/IORC#/IOWC#/INTA#/ALE/DEN/DT/R#. This mode supports multiprocessor and 8087 configurations via RQ#/GT, LOCK#, and QS [CS p.8; DS §Maximum Mode].

### 3.3 Status and byte-enable decode tables [DS §Pin Description]
| S2# S1# S0# | Cycle |  | S4 S3 | Segment |  | QS1 QS0 | Queue status |
|---|---|---|---|---|---|---|---|
| 000 | Interrupt acknowledge | | 00 | ES (alternate data) | | 00 | No operation |
| 001 | Read I/O port | | 01 | SS | | 01 | First byte of opcode from queue |
| 010 | Write I/O port | | 10 | CS or none | | 10 | Empty the queue |
| 011 | Halt | | 11 | DS | | 11 | Subsequent byte from queue |
| 100 | Code access | | | | | | |
| 101 | Read memory | | | | | | |
| 110 | Write memory | | | | | | |
| 111 | Passive | | | | | | |

| BHE# | A0 | Transfer |
|---|---|---|
| 0 | 0 | Whole word (D15–D0) |
| 0 | 1 | Upper byte, odd address (D15–D8) |
| 1 | 0 | Lower byte, even address (D7–D0) |
| 1 | 1 | None |

---

## 4. Bus cycles and timing
### 4.1 Basic bus cycle [DS §Bus Operation, §Waveforms; CS pp.6–9 Figs 5–8]
| State | Activity |
|---|---|
| **T1** | Address on AD/A lines; ALE pulse latches it (min mode); M/IO# and DT/R# valid; BHE# valid |
| **T2** | Address floats; status S3–S7 appear; RD# or WR# goes active; DEN# active |
| **T3** | READY sampled; read data sampled at the end of T3 (or of the last Tw) |
| **Tw** | Inserted while READY is low (slow memory/peripheral) |
| **T4** | Strobes go inactive; bus ends the cycle |
| **Ti** | Idle states between cycles when the queue is full and the EU needs no operand |

Minimum cycle = 4 clocks (800 ns at 5 MHz). An interrupt acknowledge uses **two consecutive INTA# cycles**: the CPU reads the type byte on D7–D0 during the second. In max mode, LOCK# is held between the two [DS §Interrupt Operations].

### 4.2 Key AC parameters, minimum mode (verify against DS §A.C. Characteristics, TA 0–70 °C, VCC 5 V ±10%)
| Symbol | Parameter | 8086 (5 MHz) | 8086-2 (8 MHz) | 8086-1 (10 MHz) |
|---|---|---|---|---|
| TCLCL | CLK period | 200–500 ns | 125–500 ns | 100–500 ns |
| TCLCH | CLK low time | ≥ (2/3·TCLCL) − 15 = 118 ns | ≥ 68 ns | ≥ 53 ns |
| TCHCL | CLK high time | ≥ (1/3·TCLCL) + 2 = 69 ns | ≥ 44 ns | ≥ 39 ns |
| TCH1CH2 / TCL2CL1 | CLK rise / fall (1.0↔3.5 V) | ≤ 10 ns | ≤ 10 ns | ≤ 10 ns |
| TDVCL | Data-in setup | ≥ 30 ns | ≥ 20 ns | ≥ 5 ns (verify) |
| TCLDX | Data-in hold | ≥ 10 ns | ≥ 10 ns | ≥ 10 ns |
| TRYHCH | READY setup into 8086 | ≥ (2/3·TCLCL) − 15 | same formula | same formula |
| TCHRYX | READY hold | ≥ 30 ns | ≥ 20 ns (verify) | ≥ 20 ns (verify) |
| TINVCH | INTR/NMI/TEST setup | ≥ 30 ns | ≥ 15 ns (verify) | ≥ 15 ns (verify) |
| TCLAV | Address valid delay | 10–110 ns | 10–60 ns | 10–50 ns (verify) |
| TLHLL | ALE width | ≥ TCLCH − 20 | ≥ TCLCH − 10 | ≥ TCLCH − 10 |
| TCLRL / TCLRH | RD# active / inactive delay | 10–165 / 10–150 ns | 10–100 / 10–80 ns | (verify) |
| TRLRH | RD# width | ≥ 2·TCLCL − 75 | ≥ 2·TCLCL − 50 | ≥ 2·TCLCL − 40 |
| TWLWH | WR# width | ≥ 2·TCLCL − 60 | ≥ 2·TCLCL − 40 | ≥ 2·TCLCL − 35 |
| TRHAV | RD# inactive → next address | ≥ TCLCL − 45 | ≥ TCLCL − 40 | ≥ TCLCL − 35 |
| TOLOH / TOHOL | Output rise / fall | ≤ 20 / ≤ 12 ns | ≤ 20 / ≤ 12 ns | ≤ 20 / ≤ 12 ns |

AC test conditions: outputs loaded with CL = 20–100 pF; inputs driven at 0.45 V / 2.4 V; timing measured at 1.5 V. Clock levels are measured at 0.6 V / 3.9 V [DS §A.C. Testing Input, Output Waveform] (verify). The max-mode tables add 8288 command delays, RQ#/GT timing, and status active/inactive delays (TCHSV, TCLSH) [DS §A.C. Characteristics, Max Mode].

### 4.3 Bus arbitration
- **Min mode:** HOLD is asserted, the CPU finishes the current cycle and raises HLDA, then floats the bus. HOLD is sampled on the rising CLK edge [DS §Bus Hold].
- **Max mode:** RQ#/GT# uses a one-wire handshake of three pulses: request, grant, and release. RQ#/GT0# has priority over GT1# [DS §Maximum Mode].

---

## 5. Interrupts [CS pp.8–9; DS §External Interface → Interrupt Operations]
| Source | Type | Trigger | Maskable? | Priority (high→low) |
|---|---|---|---|---|
| Divide error | 0 | DIV/IDIV overflow | No | 1 (internal: div error, INT n, INTO) |
| INT n / INT 3 / INTO | n / 3 / 4 | Instruction | No | 1 |
| NMI | 2 | Rising edge; hold high ≥ 2 CLK (verify) | No | 2 |
| INTR | from bus (type byte) | Level; sampled at the last clock of an instruction | IF | 3 |
| Single-step | 1 | TF = 1 | TF | 4 |

**Sequence:** push FLAGS, clear IF and TF, push CS, push IP, then load IP from `[type×4]` and CS from `[type×4+2]`. Return is via IRET [CS p.8].

**Testable edge cases:**
- Interrupts are inhibited for one instruction after `MOV SS`/`POP SS` and after prefixes.
- REP string instructions are interruptible and resume.
- Types 0–31 are reserved by Intel [DS §Interrupt Operations]. The cheatsheet mentions "unused opcode (type 6) / escape (type 7)" [CS p.9]. Those apply to later x86 chips: the original 8086 does **not** trap invalid opcodes, so this is a useful test-expectation trap.

**RESET** [DS §Processor RESET and Initialization]: CS = FFFFh, IP = 0000h, DS = SS = ES = 0000h, FLAGS cleared (IF = 0), queue empty. The first fetch is at FFFF0h, about 7 clocks after RESET falls. During reset, 3-state outputs float and the other outputs go to defined inactive levels.

**HALT:** HLT signals a halt (S2#–S0# = 011 in max mode; in min mode an ALE pulse with no strobes). The CPU leaves halt on INTR (if IF = 1), NMI, or RESET [DS §HALT].

---

## 6. Electrical characteristics relevant to test
### 6.1 Absolute maximum ratings [DS §Absolute Maximum Ratings] (verify)
| Parameter | Rating |
|---|---|
| Ambient temperature under bias | 0 °C to 70 °C |
| Storage temperature | −65 °C to +150 °C |
| Voltage on any pin vs GND | −1.0 V to +7 V |
| Power dissipation | 2.5 W |

### 6.2 DC characteristics, TA 0–70 °C, VCC 5 V ±10% [DS §D.C. Characteristics] (verify)
| Symbol | Parameter | Min | Max | Condition |
|---|---|---|---|---|
| VIL | Input low | −0.5 V | +0.8 V | |
| VIH | Input high | 2.0 V | VCC + 0.5 V | |
| VOL | Output low | — | 0.45 V | IOL = 2.5 mA |
| VOH | Output high | 2.4 V | — | IOH = −400 µA |
| ICC | Supply current | — | 340 mA (8086), 350 mA (8086-2), 360 mA (8086-1) | TA = 25 °C |
| ILI | Input leakage | — | ±10 µA | 0 V ≤ VIN ≤ VCC |
| ILO | Output leakage | — | ±10 µA | 0.45 V ≤ VOUT ≤ VCC |
| VCL / VCH | CLK input low / high | −0.5 / 3.9 V | 0.6 / VCC + 1.0 V | |
| CIN / CIO | Input / I/O capacitance | — | 15 pF | fc = 1 MHz |

The 8086 is NMOS/HMOS, so ICC is nearly constant and largely independent of clock frequency. This is different from the 80C86 (CMOS), whose current scales with frequency and which can stop its clock.

---

## 7. Proposed test areas and team split
The split assumes a bench with an 8284A clock generator, a socketed 8086 and a ROM at FFFF0h, a logic analyzer on the bus and status pins, and a programmable supply. It works for 3 people (merge workstreams D and E) up to 5. Each person owns a workstream and its pass/fail limits taken from the datasheet.

| # | Workstream | Owner focus | Primary refs |
|---|---|---|---|
| A | Functional / bring-up | Reset, mode strap, bus protocol, interrupts | DS §Pin Description, §RESET, §Interrupt Operations |
| B | Timing (AC) | Clock limits, setup/hold, strobe widths, wait states | DS §A.C. Characteristics, §Waveforms |
| C | Electrical (DC) | Levels, leakage, ICC, supply/temperature corners | DS §D.C. Characteristics, §Absolute Max |
| D | Instruction-level | ALU/flags, addressing, strings, control flow | DS §Instruction Set Summary; CS pp.6–12 |
| E | Max mode and system | 8288 status, queue status, RQ/GT, LOCK, 8087 handshake | DS §Maximum Mode; CS p.8 |

### A — Functional / bring-up
| ID | Test | Stimulus | Expected / pass criterion |
|---|---|---|---|
| A1 | Reset vector | Hold RESET ≥ 4 CLK, release | First bus cycle is a code fetch at FFFF0h (CS = FFFF, IP = 0); about 7 CLK after RESET falls |
| A2 | Reset register state | Boot ROM dumps DS/SS/ES/FLAGS to I/O | All zero; IF = 0 |
| A3 | Byte-lane enables | Word/byte reads and writes at even and odd addresses | BHE#/A0 match the §3.3 table; odd word = 2 cycles |
| A4 | INTR / INTA | Assert INTR with IF = 1; supply type 40h on D7–D0 | Two INTA# cycles; vector fetched from 00100h |
| A5 | NMI edge vs level | Hold NMI high continuously | Exactly one type-2 entry per rising edge |
| A6 | HOLD/HLDA | Assert HOLD mid-program | HLDA after the current cycle; AD, A, RD#, WR#, M/IO# float; resumes cleanly |
| A7 | HLT exit | Execute HLT, then pulse NMI | Halt status, then NMI service |

### B — Timing (AC)
| ID | Test | Method | Pass criterion |
|---|---|---|---|
| B1 | Fmax / Fmin per grade | Sweep the 8284A crystal (8284A divides by 3: 6 → 24 MHz xtal covers 2–8 MHz for an 8086-2) | Runs a checksum loop correctly from TCLCL 500 ns down to the grade minimum |
| B2 | Clock duty-cycle margin | Drive CLK from a pattern generator, vary the high/low split | Fails only outside the TCLCH/TCHCL limits |
| B3 | Read data setup (TDVCL) | Delay data-valid relative to CLK with programmable memory | Correct data at ≥ spec setup; record the failing edge for margin |
| B4 | Wait-state insertion | Pull READY low for N clocks | Exactly N Tw states; TRYHCH/TCHRYX respected |
| B5 | Strobe timing | Logic analyzer / scope on ALE, RD#, WR#, DEN# | TLHLL, TRLRH, TWLWH, TCLAV, TRHAV within limits at CL = 100 pF |
| B6 | Interrupt setup (TINVCH) | Step INTR/NMI edge vs CLK | Recognized when setup ≥ spec |

### C — Electrical (DC)
| ID | Test | Method | Pass criterion |
|---|---|---|---|
| C1 | VOL / VOH | Force IOL = 2.5 mA / IOH = −400 µA on each output | VOL ≤ 0.45 V, VOH ≥ 2.4 V |
| C2 | VIL / VIH thresholds | Sweep input levels on READY, INTR, HOLD, data inputs | Correct logic at 0.8 V / 2.0 V; record the actual switching point |
| C3 | Input leakage | 0 V and VCC on each input | \|ILI\| ≤ 10 µA |
| C4 | 3-state leakage | Float outputs (RESET or HLDA), force 0.45 V and VCC | \|ILO\| ≤ 10 µA |
| C5 | ICC | Run at max frequency, TA = 25 °C | ≤ 340/350/360 mA by grade |
| C6 | VCC corners | Run functional test A1+D at 4.5 V and 5.5 V, at 0 °C and 70 °C | Passes at all corners; never exceed absolute max |
| C7 | Clock input levels | Drive CLK at VCL/VCH limits (0.6 V / 3.9 V) | Stable operation |

### D — Instruction-level
| ID | Test | Example | Expected |
|---|---|---|---|
| D1 | ADD flags | `MOV AL,7Fh; ADD AL,1` | AL = 80h, OF = 1, SF = 1, ZF = 0, AF = 1, PF = 0, CF = 0 |
| D2 | INC preserves CF | `STC; INC AX` | CF still 1 |
| D3 | Divide error | `MOV AX,1; MOV BL,0; DIV BL` | Type-0 interrupt; stacked IP is checked against the expected 8086 behavior (points past the DIV on the 8086) |
| D4 | Addressing / segment defaults | `MOV AX,[BP+2]` vs `MOV AX,[BX+2]` | Bus S4 S3 = 01 (SS) vs 11 (DS) |
| D5 | Segment math wrap | `MOV ES,FFFFh; MOV BYTE ES:[0010h],AAh` | Write appears at 00000h (20-bit wrap, no A20) |
| D6 | REP string + DF | `STD; REP MOVSW` with CX = 4 | 4 words copied downward; SI/DI −8; CX = 0 |
| D7 | Interrupted REP | Raise INTR mid-`REP MOVSB` | Copy completes correctly after IRET; CX consistent |
| D8 | Shift count unmasked | `MOV CL,33; SHL AX,CL` | AX = 0 (count not masked to 5 bits, unlike the 286 and later) |
| D9 | FLAGS identity | `PUSHF; POP AX` after `XOR AX,AX; PUSH AX; POPF` | Bits 15–12 read as 1 on a genuine 8086 |
| D10 | Single-step | Set TF via POPF, run 3 instructions | Three type-1 entries; TF clear inside the handler |
| D11 | Invalid opcode | Execute `0Fh` (POP CS) / `D6h` | No type-6 trap on the 8086; documents the cheatsheet discrepancy |

### E — Max mode and system
| ID | Test | Method | Expected |
|---|---|---|---|
| E1 | Status decode | Strap MN/MX# = 0, run mixed I/O, memory, and HLT | S2#–S0# sequence matches the §3.3 table; 8288 commands correct |
| E2 | Queue status | Log QS1/QS0 across a taken jump | 10 (empty) after the jump; 01/11 on opcode/subsequent bytes |
| E3 | LOCK# | `LOCK XCHG [mem],AX` | LOCK# active across both bus cycles; also during INTA pairs |
| E4 | RQ#/GT# handshake | External master pulses RQ#/GT0# | Grant pulse after the current cycle; release returns the bus; GT0 beats GT1 |
| E5 | TEST#/WAIT | Execute WAIT with TEST# high, then lower it | CPU stalls, then resumes within spec |

### Suggested assignment for a 3–5 person demo
| Team size | Split |
|---|---|
| 3 | (A + E) functional and system · (B + C) timing and electrical · (D) instruction-level |
| 4 | A · B · C · (D + E) |
| 5 | A · B · C · D · E |

Shared artifacts the team should agree on first:
1. A pinned copy of DS 231455-006 with the verified limits table (see the verification banner at the top).
2. A common boot ROM and test harness that reports results over an I/O port.
3. A results matrix: test ID × speed grade × VCC × temperature.
