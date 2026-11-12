# Decisions

Decisions taken at M0 for the open items in [design.md §13](design.md#13-open-decisions), plus the
interface choices M0 had to make where the design doc only sketched them.

## Open decisions from design.md §13

| # | Decision | Choice | Notes |
|---|---|---|---|
| D1 | Device model backend | **Behavioral C++ only** | User decision. No Verilator; M11 is out of scope. `DeviceModel` stays a pure interface, so a backend could still be added later without touching the tester or the tests. |
| D2 | Verilog core | n/a | Follows from D1. |
| D3 | Team size | **4** | User decision. Roles P1–P4 as in design.md §11; see `.github/CODEOWNERS`. |
| D4 | Unit-test framework | **GoogleTest 1.15.2** (+ gMock) | Pulled with `FetchContent`, pinned by SHA256. Each `TEST` becomes its own `ctest` entry. |
| D5 | Spec format and parser | **YAML + yaml-cpp 0.8.0** | Pinned by SHA256. Supports comments and provenance next to each value. |
| D6 | Pattern format | **Custom `.pat`** (design.md §6.2) | `tester::Pattern` header frozen; parser lands in M4. |
| D7 | Test ROM toolchain | **NASM, `cpu 8086`** | Not needed for M0. M5 adds `find_program(NASM)` and the ROM build rules. |
| D8 | Max mode in demo scope | **Stretch** (M9) | |
| D9 | Shmoo rendering | **Native SVG + ASCII** | No Python dependency. |
| D10 | Repo and CI | **github.com/liwillbiz/8086-eda-demo, GitHub Actions** | `ubuntu-24.04`, GCC, `./regress` plus an ASan/UBSan unit-test job. |
| D11 | Limits verification | **In parallel (X1)** | `verified: false` flags in `spec.yaml` until done. |

## M0 build and interface choices

- **CMake ≥ 3.25, not 3.24.** `cmake --workflow` (used by `./regress`) needs preset schema v6, which needs 3.25.
- **Schema validation** will be a hand-written validator in `libs/spec`, run under `ctest` (M1). `spec/schema/spec.schema.json` documents the shape for editors and reviewers; we don't add a JSON-Schema library.
- **Stubs throw `i8086::NotImplemented`** (in the header-only `libs/common`, which was added for this). The runner reports it as **Error**, never Fail, so an unfinished layer can't look like a device defect. Each message names the milestone that replaces it (for example `[M4]`).
- **`Spec` uses pimpl** (`shared_ptr<const Impl>`), so it is cheap to copy and M1 can change storage without editing the frozen header.
- **`FaultInjector` is a `DeviceModel` decorator** (`model/fault_injector.hpp`) that applies the pin-level faults (StuckAt, SlowTiming, SetupDegrade, LowVccFail). `LogicBug` is passed into the behavioral core, and `Leaky` is consumed by `tester::ElectricalModel`. `make_behavioral()` always returns the core wrapped in a `FaultInjector`.
- **Golden independence is enforced by the build:** `i86golden` links only `i86::common`, never `i86::model`. It has its own `Registers` and `Program` types.
- **Additions to the design doc sketches:**
  - `Tester::set_temperature()`, because the sketch had no way to set TA for the VCC × TA matrix.
  - `measure_time(EdgeRef from, EdgeRef to)`: the pin moved into `EdgeRef`, so a measurement can span two pins (for example CLK → ALE).
  - `TestContext` also carries `vcc_v` and `temp_c`.
  - `Spec::grade()` returns `fclk_max` and the TCLCL range.
  - `TestCase::spec_symbols()` is optional.
- **Datalog JSON** uses generic `value` / `min` / `max` / `unit` keys instead of unit-suffixed keys such as `value_ns`.
- **Test cases are compiled as a CMake OBJECT library** (`i86cases`, globbed from `testprog/cases/**.cpp`), so `REGISTER_TEST` static initializers are never dropped by the linker.
- **Pin names** use the minimum-mode name with `_N` for active-low pins (`RD_N`, `M_IO_N`). Pin 20 is `GND2` so that names stay unique.
- **Baselines:** `./regress` diffs `out/good/results.csv` against `regression/baselines/good_results.csv`. It is header-only until test cases exist; shmoo baselines come in M6.
- **Interface change policy:** the headers under `libs/*/include` and `testprog/include` are frozen. Additive changes are fine through a PR reviewed by the Spec & Integration lead and the owner of that header (enforced by CODEOWNERS). Breaking changes need every affected owner.

## M1 spec choices

- **No frozen header changed.** `spec.hpp` is identical to M0; everything new lives behind `Spec::Impl`.
- **Expressions** support `+ - * /`, parentheses, numbers, and variables. AC bounds may use `TCLCL` (the period passed to `Spec::ac()`) and `SYM.min` / `SYM.max` of another AC parameter at the same grade and period (for example `TLHLL = TCLCH.min - 20`). A limit that references an unverified limit is itself reported as unverified. `Spec::ac("TCLCL", …)` returns the `grades` range, so TCLCL is not duplicated under `ac`.
- **`Spec::ac()` evaluates at any positive period**, including periods outside the grade's range, because the shmoo sweeps below the grade minimum.
- **VCC-relative DC limits** (`VIH max = VCC + 0.5`, `VCH max = VCC + 1.0`): `Spec::dc(sym, Grade)` has no VCC argument, so it returns the bound that holds over the whole `conditions.VCC` range (a max takes its smallest value, a min its largest). Today that gives VIH max = 5.0 V and VCH max = 5.5 V. If M6 needs the limit at the actual test VCC, add an overload `dc(sym, Grade, double vcc_v)`; the expressions are already stored.
- **`conditions`** (VCC, TA, AC test levels and load) are loaded and validated, and VCC drives the DC evaluation above. There is no public accessor yet; M4/M6 can add `Limit condition(std::string_view) const` additively if the tester needs them.
- **`dc.*.cond`** (for example `IOL_mA: 2.5`) is validated but not exposed through `Limit`.
- **Decode tables** hold stable identifiers (`int_ack`, `mem_read`, `CS_or_none`, `high_byte`, …), not prose, so the protocol checker can compare strings.
- **`verified: false`** marks every value the brief flags "(verify)": the whole of brief §6.1 and §6.2 (and the VCC/TA conditions they share), the clock measurement levels in the AC test conditions, and the individual §4.2 cells (8086-1 TDVCL, TCLAV, TCLRL, TCLRH; 8086-2/-1 TCHRYX and TINVCH). The 8086-1 TCLRL/TCLRH cells have no value in the brief; they carry the 8086-2 values as placeholders until X1.
- **8086-1 TCLCH and TCHCL** are the fixed 53 ns and 39 ns the brief prints, not the formula (which would give 51.7 ns and 35.3 ns at 100 ns). The 8086 and 8086-2 columns use the formula.
- **Validation is strict**: unknown or duplicate keys, missing `ref`/`verified`, unknown units, bad expressions, unknown variables or references, reference cycles, min > max at either end of a grade's clock range, incomplete decode tables, and pin-table errors all fail `Spec::load` with `file:line`. `libs/spec/tests/validation_test.cpp` covers each rule by mutating the real `spec.yaml`.

## M2 golden emulator choices

- **No frozen header changed.** `emulator.hpp` is identical to M0; the core lives in `libs/golden/src/cpu.{hpp,cpp}`. One **additive** header, `golden/rom.hpp`, adds the ROM loader (`read_binary`, `rom_program`, `load_rom`). It places a flat binary so that its last byte is at FFFFFh, which puts the reset entry FFFF0h 16 bytes from its end. Because it is under `libs/golden/include/`, CODEOWNERS asks the Spec & Integration lead to review it.
- **Coverage:** the full §7.3 subset, plus most of the rest of the 8086 ISA, because the decode is shared: all ALU forms, SBB/NEG/NOT/TEST, every shift and rotate, MUL/IMUL/IDIV, the BCD adjusts, every string op with REP/REPE/REPNE, LOOP*/JCXZ, XCHG, LEA, LDS/LES, CBW/CWD, LAHF/SAHF, XLAT, INT 3/INTO. **`StopReason::Unimplemented`** covers WAIT (TEST# is not part of a `Program`), the undocumented D0–D3 /6, FE /2–/7, FF /7, 8F/C6/C7 with reg ≠ 0, and LEA/LDS/LES/far CALL/far JMP with a register operand. ESC (D8–DF) decodes its operand and does nothing, which is what an 8086 does without an 8087.
- **Undocumented opcodes that are implemented** (brief D11): `0F` = POP CS, `D6` = SALC (AL = CF ? FFh : 00h), `60–6F` = aliases of `70–7F`, `C0/C1/C8/C9` = aliases of `C2/C3/CA/CB`, `82` = alias of `80`, `F1` = LOCK, F6/F7 /1 = TEST. 8C/8E decode only the low two bits of the segment field, so `MOV CS,r/m` is legal and acts as a jump. None of these raise type 6.
- **Stop semantics.** HLT counts as an instruction, and IP is left past it. On Unimplemented, nothing has changed, IP points at the start of the instruction (its first prefix), and the instruction is not counted. On InstructionLimit, `instructions == max_instructions`.
- **Memory** is 1 MB of zero-filled RAM. `Program::segments` is copied in order, wrapping at 1 MB. Every byte write is recorded in `mem_writes` in program order, including stack pushes and interrupt frames; a word write is two records, low byte first. Word accesses wrap **inside the segment** (a word at `xxxx:FFFF` uses `xxxx:0000` for its high byte). Physical addresses wrap at 1 MB (D5).
- **I/O.** `IN` reads `Program::io_inputs[port]`: the whole 16-bit value for a word IN, and its low byte for a byte IN. A port that is not listed reads as an undriven bus (FFh / FFFFh). Every OUT is one `IoWrite` record.
- **FLAGS representation.** `Result::regs.flags` holds only the nine real flags (mask 0FD5h), which matches `reset_registers()` (flags = 0). Bits 15–12 and bit 1 read as 1 wherever FLAGS reaches memory or AH: PUSHF, interrupt frames, and LAHF (D9). POPF, IRET, and SAHF store only the nine flags.
- **Undefined flags are left unchanged.** The model may legitimately differ, so the M5 model-vs-golden comparison must mask these per instruction:

  | Instruction | Undefined (golden leaves unchanged) |
  |---|---|
  | AND, OR, XOR, TEST | AF |
  | SHL/SHR/SAR | AF; OF when count ≠ 1 |
  | ROL/ROR/RCL/RCR | OF when count ≠ 1 |
  | MUL, IMUL | SF, ZF, AF, PF |
  | DIV, IDIV | all six arithmetic flags |
  | AAA, AAS | OF, SF, ZF, PF |
  | DAA, DAS | OF |
  | AAM, AAD | OF, AF, CF |

  A shift or rotate with a count of 0 changes no flags. The count is never masked (D8).
- **Interrupts.** `InterruptEntry::instruction_index` is the 0-based index of the instruction that raised the interrupt (INT n, INT 3, INTO, divide error), or of the instruction after which the single-step trap was taken. The divide error stacks the IP of the instruction **after** DIV/IDIV/AAM (D3). The IDIV quotient range is ±127 / ±32767: a quotient of −128 / −32768 raises type 0 on the 8086.
- **Single step (TF).** A type-1 trap follows every instruction that *started* with TF = 1, except an instruction that itself entered an interrupt, HLT (the run stops), and any segment-register load (MOV sreg, POP sreg), which on the 8086 inhibits the trap for one instruction. So POPF setting TF traps after the *next* instruction, and IRET into a TF = 1 context traps after the first instruction there (D10).
- **Known simplifications.** A REP string instruction runs to completion as one instruction; on silicon a TF trap can split it per iteration. A `Program` has no INTR, NMI, HOLD, or TEST# stimulus, so the golden cannot predict A4, A5, or D7 as-is. M8 will need an additive field on `Program` (for example, interrupt requests keyed by instruction index), which means a frozen-header review.
- **Tests** (`libs/golden/tests`): brief D1–D6 and D8–D11 vectors, a shift/rotate table, multiply/divide/BCD cases, control flow, strings, I/O capture, and the ROM loader. `flags_table_test.cpp` runs ADD/ADC/SUB/SBB/CMP/INC/DEC/NEG over every 8-bit operand pair (with and without CF in) and over 4 000 16-bit edge and fixed-seed random pairs. Results are checked against a reference written with plain signed arithmetic, and reported by the emulated program through PUSHF and OUT.
