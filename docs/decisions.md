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

## M3 model choices

- **No frozen header changed.** The behavioral core lives in `libs/model/src/behavioral/` (`core.hpp`, `core_biu.cpp`, `core_eu.cpp`). `FaultInjector` is still a pass-through decorator; M7 fills it in.
- **Edge contract.** Every T-state starts on a CLK **falling** edge: `clock_edge(Falling)` runs the EU for one clock, then advances the BIU (T1→T2→T3→Tw…→T4→T1/Ti). `clock_edge(Rising)` ends the ALE pulse, samples RESET, READY (in T3/Tw), INTR, NMI, HOLD and MN/MX#, commits read data (T4), and picks the next bus cycle, driving M/IO# and DT/R# before T1.
- **Pin events.** Each delay is a die symbol from `timing_ns`, applied at the edge the datasheet measures it from: AD/A/BHE# address at T1 and A19–A16 status at T2 (`TCLAV`), ALE (`TCLLH` / `TCHLL`), RD# (`TCLRL` / `TCLRH`), WR# and DEN# (`TCVCTV` / `TCVCTX`), M/IO# and DT/R# (`TCHCTV`), AD float (`TCLAZ`), and write data (`TCLDV`). The die profile may therefore hold datasheet symbols that `spec.yaml` doesn't have yet. A missing key fails `make_behavioral` with `SpecError`. The first `clock_edge()` reports the level of every output pin at delay 0, and later edges report only changes. **Derating is not applied in the model.** M6 decides where `k(VCC,T)` is applied (once), since `ElectricalModel::derate` lives in the tester layer.
- **Inputs.** `set_inputs` updates only the pins it is given; the others keep their level (power-on: READY = MN/MX# = 1, AD = Z, the rest 0). Each sample's `changed_ps` is checked against the die requirement at the sampling edge: read data `TDVCL` at the falling edge that starts T4, READY `TRYHCH`, and INTR/NMI/HOLD `TINVCH` at the rising edge. The **hold** check (`TCLDX`, `TCHRYX`) runs on the next `set_inputs`: a change within the hold window after the edge spoils the latch. `Conditions` has no duty-cycle field, so the model converts times using the spec's 33% CLK (low 2/3, high 1/3). If M4/M6 drive other duty cycles, add `clk_high_ns` to `Conditions` (an additive change to a frozen header).
- **X becomes wrong data.** A data bit latched as X resolves to the inverse of the level being driven, and an undriven bit (Z) reads as 1. That makes a setup or hold violation always visible downstream. A READY latched as X counts as not ready and inserts an extra Tw.
- **Bus details.** Words at even addresses take one cycle; words at odd addresses (memory or I/O) take two byte cycles, odd byte first. A byte write is driven on both lanes, and BHE#/A0 says which lane is valid. BHE#/S7 holds its T1 value (S7 is not modeled). S5 = IF, S6 = 0. Vector reads use S4 S3 = 10 ("none"). Halt is an ALE pulse with no strobes, followed by Ti; `BusCycleType::Halt` covers its T1.
- **RESET** is sampled on rising edges without a setup check (the 8284A synchronizes it). While RESET is high, 3-state outputs float, ALE and HLDA are 0, and INTA# is 1. The first T1 (code fetch at FFFF0h) starts 7 CLK after the falling edge that precedes the first low sample (brief §5). The model powers up as if RESET had just been released. A model that threw stays failed until RESET.
- **Prefetch queue.** A word fetch runs from an even IP and a byte fetch from an odd IP, only when at least 2 bytes are free. The queue therefore stops at 5 bytes when the EU has consumed an odd number of bytes. A flush discards an in-flight fetch's data.
- **EU** is a C++20 coroutine, so instruction code reads sequentially and suspends on queue bytes, bus cycles, and EU clocks. Clock counts are approximate and deterministic; the datasheet has none. Implemented: the §7.3 subset plus SBB (same ALU encoding group), INTO, single-step (TF), `0Fh` POP CS and `8Eh /1` MOV CS (these flush the queue and continue at the new CS:IP, unlike silicon, so that they match an instruction-level emulator), and `D6h` as SALC (AL = CF ? FFh : 00h, no trap). Everything else raises `UnimplementedInstruction`.
- **Undefined flags**, for the golden owner's differential fuzz to mask or match: AND/OR/XOR clear AF; shifts leave AF unchanged; for shift counts above 1, OF follows the last single-bit step; DIV leaves all flags unchanged.
- **Later milestones report `NotImplemented`** (Error, not Fail): INTR with IF = 1, a rising NMI edge, HOLD high (M8), and MN/MX# = 0 (M9).
- **LogicBug** names in the core are `inc_clears_cf` and `shift_count_masked`. An unknown name throws `NotImplemented` (M7 adds more).
- **`libs/model/tests/harness.{hpp,cpp}`** is a small memory-emulation driver: it decodes the address from the pins when ALE falls, serves memory and I/O, holds READY low, and controls data setup and hold. It shows the input protocol `SimTester` (M4) needs to follow.
