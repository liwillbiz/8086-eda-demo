# 8086 Demo EDA Test System — Design Doc

**Status:** Draft v0.1 · **Date:** 2026-11-10 · **Code repo:** not created yet
**Source of truth for device facts:** [`8086-reference-brief.md`](8086-reference-brief.md) (the "brief"). Test IDs (A1…E5) refer to its §7.

## 0. TL;DR

We build a small, fully simulated "chip tester" for the Intel 8086. It is a demo of 3–5 teammates working in parallel through Cursor Projects. It has four layers. Each layer has one owner and talks to the others only through a frozen interface:

| # | Layer | What it is | Owner role |
|---|---|---|---|
| 1 | **Spec** | `spec.yaml`: pins, AC timing per speed grade, DC limits. Every other layer reads limits from it. | Spec & Integration lead |
| 2 | **Device model** | Behavioral C++ 8086 at bus-cycle level (T1–T4/Tw/Ti), with fault-injection hooks | Model owner |
| 3 | **Tester** | Abstract ATE-like `Tester` API. `SimTester` wraps the model and adds the electrical/timing-margin model. | Tester owner |
| 4 | **Test program** | The brief's 36 tests, a pattern format, a golden emulator, datalog, summary report, and shmoo plot | Test owner |

A single command (`./regress`) builds everything, runs the unit tests, runs the 36 tests across the speed-grade × VCC matrix on a "good" die and on fault-injected dies, and diffs the outcome against a checked-in baseline. The demo story is a teammate's change getting caught by that regression.

**Non-goal:** this does not characterize real silicon. Every analog quantity is synthetic. The point is a realistic *test system architecture* and a realistic collaboration workflow.

---

## 1. Goals and non-goals

**Goals**
- Realistic ATE concepts: levels (VIH/VIL/VOH/VOL), timing sets (drive and strobe edges), vectors, pass/fail against limits, datalog, shmoo.
- Clean layer boundaries, so each teammate's agent can work independently against stubs.
- Every limit comes from `spec.yaml`. No magic numbers in the model, tester, or tests.
- Deterministic and fast. A full regression should take about a minute on a laptop.
- Fault injection, so the demo can show that the tests actually catch defects.

**Non-goals (v1)**
- Gate-level or transistor-level accuracy, or real analog simulation.
- The full 8086 instruction set on day one. Start with the subset the D-tests need (§7).
- Driving real hardware. The `Tester` abstraction leaves room for it later.

---

## 2. Architecture

```
            spec/spec.yaml  ──(read by all)──┐
                                             ▼
┌───────────────────┐  TestCase API  ┌──────────────────┐  Tester API  ┌──────────────────────────┐
│ 4. Test program   │ ─────────────▶ │ 3. Tester        │ ───────────▶ │ SimTester                │
│ cases, patterns,  │ ◀── results ── │ (abstract ATE)   │              │  ElectricalModel (margins)│
│ golden, datalog,  │                └──────────────────┘              │        │ DeviceModel API  │
│ report, shmoo     │                                                  │        ▼                  │
└───────────────────┘                                                  │ 2. Device model (C++)     │
        │ golden emulator (independent of the model)                   │    + FaultInjector        │
        └──────────── expected architectural results ──────────────────┤ (later: VerilatorBackend) │
                                                                       └──────────────────────────┘
```

**Dependency rule:** a layer may depend only on layers below it and on `spec`. The test program never touches `DeviceModel` directly; it sees only `Tester`. That keeps a future hardware tester (or the Verilator backend) a drop-in replacement.

### 2.1 Where "reality" lives: spec vs die profile
There are two kinds of numbers, and keeping them apart is what makes the test system meaningful:

| File | Meaning | Used by |
|---|---|---|
| `spec/spec.yaml` | **Limits**: what the datasheet guarantees (for example TCLAV ≤ 110 ns at 5 MHz) | Tester (levels and timing defaults), test program (pass/fail), model (pin list only) |
| `spec/dies/*.yaml` | **Simulated silicon**: what *this* part actually does (for example TCLAV = 62 ns nominal, setup requirement 14 ns, Fmax-vs-VCC curve, threshold voltages) | Device model and ElectricalModel only |

A "good" die sits inside every limit with margin. A "marginal" die sits near the limits. Fault-injected dies break specific things. The tests know only the spec, so they must *discover* how a die differs.

---

## 3. Layer 1 — Spec as data

**Owner:** Spec & Integration lead. **Artifact:** `spec/spec.yaml` plus `libs/spec` (loader, schema validation, and expression evaluator).

### 3.1 Content (all derived from the brief)
- `device`: name, package (DIP-40), speed grades `8086` / `8086-2` / `8086-1` with `fclk_max` of 5, 8, and 10 MHz.
- `pins`: number, name, direction (`in`, `out`, `io`, `pwr`), 3-state flag, active-low flag, and mode alias (min/max) for pins 24–31 (brief §3).
- `ac`: per-grade min/max for every parameter in brief §4.2. Formulas such as `2*TCLCL-75` are kept as **expressions** evaluated at the test clock period.
- `dc`: brief §6.2 (VIL, VIH, VOL@IOL, VOH@IOH, ICC per grade, ILI, ILO, VCL/VCH, CIN).
- `abs_max`: brief §6.1. The tester refuses to program levels beyond these.
- `conditions`: VCC 4.5–5.5 V, TA 0–70 °C, AC test levels (0.45/2.4 V drive, 1.5 V measure), CL 20–100 pF.
- `decode`: the S2–S0, S4/S3, QS, and BHE#/A0 tables (brief §3.3), which the protocol checker uses.
- Provenance on **every** entry: `ref: "DS §A.C. Characteristics"` and `verified: false|true`. The brief marks many values "(verify)". Those carry `verified: false`, and the report flags any pass/fail decision that rests on an unverified limit.

### 3.2 Format sketch
```yaml
schema_version: 1
device: { name: i8086, package: DIP40 }
grades:
  "8086":   { fclk_max_mhz: 5,  tclcl_ns: [200, 500] }
  "8086-2": { fclk_max_mhz: 8,  tclcl_ns: [125, 500] }
  "8086-1": { fclk_max_mhz: 10, tclcl_ns: [100, 500] }
pins:
  - { num: 25, name: ALE,  dir: out, mode: min, max_mode_name: QS0 }
  - { num: 32, name: RD_N, dir: out, tristate: true, active_low: true }
  - { num: 16, name: AD0,  dir: io,  tristate: true, bus: AD, bit: 0 }
ac:
  TCLAV: { desc: "Address valid delay", unit: ns, ref: "DS §A.C.",
           "8086": { min: 10, max: 110 }, "8086-2": { min: 10, max: 60 },
           "8086-1": { min: 10, max: 50, verified: false } }
  TRLRH: { desc: "RD# width", unit: ns, ref: "DS §A.C.",
           "8086": { min: "2*TCLCL-75" }, "8086-2": { min: "2*TCLCL-50" },
           "8086-1": { min: "2*TCLCL-40" } }
dc:
  VOL: { max: 0.45, unit: V, cond: { IOL_mA: 2.5 }, ref: "DS §D.C.", verified: false }
  ICC: { unit: mA, "8086": { max: 340 }, "8086-2": { max: 350 }, "8086-1": { max: 360 } }
```

### 3.3 Interface
```cpp
namespace i8086::spec {
struct Limit { std::optional<double> min, max; std::string unit, ref; bool verified; };
enum class Grade { G8086, G8086_2, G8086_1 };
struct Pin { int num; std::string name; Dir dir; bool tristate, active_low; std::optional<std::string> max_mode_name; };

class Spec {
 public:
  static Spec load(const std::filesystem::path&);         // throws SpecError with file:line
  const std::vector<Pin>& pins() const;
  const Pin& pin(std::string_view name) const;
  Limit ac(std::string_view sym, Grade, double tclcl_ns) const;  // evaluates expressions
  Limit dc(std::string_view sym, Grade) const;
  Limit abs_max(std::string_view sym) const;
  const DecodeTables& decode() const;
};
}
```
A schema check (JSON Schema or a hand-written validator) runs in `ctest`, so a typo in `spec.yaml` fails the build, not the demo.

---

## 4. Layer 2 — Device model

**Owner:** Model owner. **Artifact:** `libs/model`.

### 4.1 Scope
- **Timing granularity:** one call per CLK half-cycle. Within each half-cycle the model reports output transitions as `(pin, value, delay_ps after the edge)`. The delays come from the die profile and are scaled by fault and derating factors.
- **BIU:** 6-byte prefetch queue (fetches when ≥ 2 bytes are free), T1–T4, Tw while READY is low (sampled in T3), Ti when idle, queue flush on branches, odd-address words split into two cycles, and BHE#/A0 per brief §3.3.
- **Pins (min mode first):** AD15–0, A19/S6–A16/S3, BHE#/S7, ALE, RD#, WR#, M/IO#, DT/R#, DEN#, INTA#, HOLD/HLDA, READY, INTR, NMI, TEST#, RESET, MN/MX#. Max mode (S2#–S0#, QS1/QS0, LOCK#, RQ#/GT#) comes in milestone M9.
- **EU:** decode and execute from the queue; flags; interrupts (type 0–4, INTR via two INTA# cycles, NMI edge); RESET state (brief §5); HLT. The v1 instruction subset is the set the D-tests and the boot ROM use (§7.3).
- **Input sampling:** the model latches an input only if it was stable for the die's setup and hold window around the sampling edge. Otherwise it latches `X`, and X turns into wrong data downstream. This is how timing margins become *functional* failures, which is what B3, B4, and B6 detect.

### 4.2 Fault-injection hooks
| Fault | Parameters | Expected to be caught by |
|---|---|---|
| `StuckAt` | pin, value (0/1), optional start cycle | A1, A3, D-suite (data pins); A4–A7 (control pins) |
| `SlowTiming` | parameter(s) or `all`, scale factor or +ns | B5 (strobe widths and delays), B1 (Fmax) |
| `SetupDegrade` | input class (data/READY/INTR), +ns | B3, B4, B6 |
| `LowVccFail` | VCC floor (V) below which the EU corrupts results or the clock stalls | C6 corners, shmoo |
| `Leaky` | pin, leakage µA | C3, C4 (via ElectricalModel) |
| `LogicBug` | named behavioral bug (for example `inc_clears_cf`, `shift_count_masked`) | D2, D8, … |

Faults are loaded from `spec/dies/*.yaml` or added through the API. They are applied in one `FaultInjector` layer, so model logic stays clean.

### 4.3 Interface
```cpp
namespace i8086::model {
enum class Logic : uint8_t { L0, L1, Z, X };
struct PinEvent { int pin; Logic value; int64_t delay_ps; };       // relative to the edge
struct Conditions { double vcc_v; double temp_c; double tclcl_ns; };

class DeviceModel {                                    // also implemented by VerilatorBackend
 public:
  virtual ~DeviceModel() = default;
  virtual void set_conditions(const Conditions&) = 0;  // drives derating and LowVccFail
  virtual void set_inputs(std::span<const InputSample>) = 0;  // value plus time of last change
  virtual std::vector<PinEvent> clock_edge(Edge rising_or_falling) = 0;
  virtual ArchState arch_state() const = 0;           // debug and white-box unit tests only
  virtual BusState bus_state() const = 0;             // T-state, cycle type (for the protocol checker)
};
std::unique_ptr<DeviceModel> make_behavioral(const spec::Spec&, const DieProfile&, FaultSet);
}
```

### 4.4 Optional later backend: Verilator (decision still open)
- **Recommendation: hybrid, C++ first.** Ship the behavioral model (fast, easy to add fault hooks). Later, add `VerilatorBackend` implementing the same `DeviceModel` interface around an open-source Verilog 8086 core, and run the same test program against both.
- Benefits: an "RTL vs behavioral" cross-check, and it shows that the tester abstraction is backend-agnostic.
- Cost and risk: a core still has to be chosen (license, min/max-mode support, cycle accuracy). Fault hooks need RTL-level forcing. Verilator also adds a toolchain dependency. **The user has not decided yet; see §13.**

---

## 5. Layer 3 — Tester interface

**Owner:** Tester owner. **Artifact:** `libs/tester` (abstract API plus `SimTester` and `ElectricalModel`).

### 5.1 Concepts (mirroring ATE)
- **Levels set:** VCC, VIH, VIL per pin group; VOH/VOL compare thresholds; IOL/IOH loads. These are checked against `abs_max`.
- **Timing set:** period (TCLCL), CLK duty, per-pin drive format (NRZ, RZ, R1), drive edge (ns into the period), and strobe edge or window for compares.
- **Pattern:** a sequence of vectors, one per CLK period, each with a timing-set index. It is loaded from a `.pat` file (§6.2) or generated.
- **Memory-emulation mode:** the tester acts as the system's memory and I/O. It decodes the bus cycles the DUT runs, serves a ROM image, and records writes and I/O. Most functional tests run this way because CPU vectors are data-dependent.
- **Measurements:** functional pass/fail with failing-vector capture, `measure_time(pin, edge)`, and PMU-style DC measurements (`force_current_measure_voltage`, `force_voltage_measure_current`, `measure_icc`).
- **Search:** binary or linear search of a level or edge until pass→fail. This is the primitive behind margins and shmoo.

### 5.2 How margins are modeled without analog simulation
Everything analog lives in `ElectricalModel`, which is driven by the die profile and the current `Conditions`:

| Quantity | Simulated how |
|---|---|
| Input thresholds | Each input has a die threshold voltage (for example 1.4 V ± offset). A driven VIH below it reads as 0; between thresholds it reads as X. C2 then measures where the switching point is. |
| Output levels | VOL = V0 + R_on·IOL, VOH = VCC − ΔV − R_on·IOH, with per-die R_on. C1 checks them against the limits. |
| Timing derating | Every model delay is multiplied by `k(VCC,T) = ((Vnom−Vt)/(VCC−Vt))^α · (1+β·(T−25))`. The setup requirement scales the same way. This produces a realistic Fmax(VCC) curve for the shmoo. |
| Load | Output delay += CL × slope (ns/pF), so B5 can test at CL = 100 pF. |
| Leakage, ICC | Per-pin leakage and an ICC that is roughly independent of frequency (NMOS, brief §6.2) plus a VCC term; `Leaky` adds to it. |

### 5.3 Interface
```cpp
namespace i8086::tester {
struct LevelSet { double vcc, vih, vil, voh, vol; double iol_ma, ioh_ua; };
struct PinTiming { DriveFormat fmt; double drive_ns; double strobe_ns; };
struct TimingSet { double period_ns; double clk_high_ns; std::map<std::string, PinTiming> pins; };
struct FunctionalResult { bool pass; std::vector<VectorFail> fails; uint64_t cycles; };

class Tester {
 public:
  virtual ~Tester() = default;
  virtual void set_levels(const LevelSet&) = 0;                       // throws if > abs_max
  virtual void set_timing(int id, const TimingSet&) = 0;
  virtual void power(bool on) = 0;
  virtual FunctionalResult run_pattern(const Pattern&) = 0;            // vector mode
  virtual FunctionalResult run_program(const MemoryImage&, const RunOpts&) = 0;  // memory-emulation mode
  virtual const BusTrace& last_trace() const = 0;                      // per-cycle pin log for checkers
  virtual double measure_time(std::string_view pin, EdgeRef from, EdgeRef to) = 0;  // ns
  virtual double force_i_measure_v(std::string_view pin, double ma) = 0;
  virtual double force_v_measure_i(std::string_view pin, double v) = 0;  // µA
  virtual double measure_icc() = 0;                                    // mA
};

class SimTester final : public Tester {
 public:
  SimTester(const spec::Spec&, std::unique_ptr<model::DeviceModel>, ElectricalModel);
};

template <class F> double search_pass_fail(F run_at, double lo, double hi, double resolution);
}
```
**Time base:** integer picoseconds everywhere, so results are deterministic and carry no floating-point edge jitter.

---

## 6. Layer 4 — Test program

**Owner:** Test owner (plus the golden-emulator owner in a 5-person team).

### 6.1 Components
- **Test cases:** `TestCase` subclasses registered by brief ID (A1…E5). Each declares the tester resources it needs and the spec symbols it judges against.
- **Boot ROM and test ROMs:** short 8086 assembly programs placed at FFFF0h that report results by writing to I/O ports (brief §7, "shared artifacts" item 2). They are assembled at build time (NASM in `cpu 8086` mode is the proposed tool; see §13).
- **Golden reference:** `libs/golden`, a simple instruction-level 8086 emulator with no bus timing. It runs the same ROM and predicts final registers, flags, memory and I/O writes, and interrupt entries. It must be written **independently of the device model** (different owner, no shared execute code), so that bugs in one show up as mismatches against the other.
- **Protocol checker:** walks `BusTrace` and checks the T-state order, ALE/RD#/WR#/DEN# sequencing, the status and BHE# decode, and INTA# pairs against `spec.decode`.
- **Judge:** compares measured values against `spec.ac/dc` limits for the active grade and clock period, and attaches `verified` provenance to every result.
- **Datalog, summary, and shmoo writers** (§6.3–6.5).
- **Runner CLI:** `i86test --spec spec/spec.yaml --die spec/dies/good.yaml --grade 8086-2 --vcc 4.5,5.0,5.5 --tests A*,D1-D11 --out out/`

```cpp
namespace i8086::testprog {
struct TestContext { tester::Tester& t; const spec::Spec& spec; spec::Grade grade; Datalog& log; const golden::Emulator* golden; };
enum class Verdict { Pass, Fail, Error, Skip };
class TestCase {
 public:
  virtual ~TestCase() = default;
  virtual std::string_view id() const = 0;            // "B3"
  virtual std::string_view title() const = 0;
  virtual Verdict run(TestContext&) = 0;
};
#define REGISTER_TEST(cls) /* static registration into TestRegistry */
}
```

### 6.2 Pattern / vector format (`.pat`, text, diff-friendly)
```
# reset_vector.pat — A1
pattern reset_vector
spec    spec/spec.yaml
pins    RESET READY INTR NMI HOLD MN_MX  ALE RD_N WR_N M_IO  AD[15:0]           A[19:16]
timing  0 period=TCLCL drive=0ns  strobe=TCLAV.max+5ns     # symbolic refs into spec
#       ---- drive ----------- | ---- compare ----------------------------------
v 0     1     1     0    0   0  1      X    X    X    X     XXXXXXXXXXXXXXXX    XXXX   repeat 4
v 0     0     1     0    0   0  1      X    X    X    X     XXXXXXXXXXXXXXXX    XXXX   repeat 7
v 0     0     1     0    0   0  1      H    H    H    H     HHHHHHHHHHHHLLLL    HHHH   # T1: addr FFFF0h
```
Characters: drive `0 1 Z`; compare `L H` (against VOL/VOH), `X` (don't care), `M` (expect Z); `C` captures the value into the datalog. Timing values may reference spec symbols, so a spec change flows into the patterns automatically.

### 6.3 Datalog (`out/datalog.jsonl`, one record per measurement)
```json
{"run":"2026-11-10T21:30Z#a1b2","test":"B5","grade":"8086-2","vcc":4.5,"temp":70,"die":"good",
 "param":"TRLRH","value_ns":201.3,"min_ns":200.0,"max_ns":null,"result":"pass","margin_ns":1.3,
 "limit_ref":"DS §A.C.","limit_verified":false}
```

### 6.4 Summary report (`out/summary.md` plus `out/results.csv`)
- A results matrix (brief §7, "shared artifacts" item 3): test ID × grade × VCC × temperature, with pass/fail per cell.
- Worst-margin table per AC/DC parameter.
- An "unverified limit" banner listing results that relied on `verified: false` values.
- Fault-coverage table (regression only): fault × tests that caught it.

### 6.5 Shmoo (VCC vs frequency)
- Grid: VCC 4.0–6.0 V (clamped to `abs_max`) × TCLCL from 500 ns down to 80 ns, running a chosen test (default: the B1 checksum loop plus D-suite).
- Outputs: `shmoo.csv`, an ASCII plot for terminals and PRs, and `shmoo.svg` (written natively; no Python dependency). The spec box (grade fmax × 4.5–5.5 V) is overlaid, so a die whose fail region intrudes into the box is visibly out of spec.
```
VCC\MHz  4  5  6  7  8  9 10 11 12
 5.5     *  *  *  *  *  *  *  *  .
 5.0     *  *  *  *  *  *  *  .  .      * pass  . fail  [box] = 8086-2 spec
 4.5     *  *  *  *  *  *  .  .  .
 4.0     *  *  *  .  .  .  .  .  .
```

---

## 7. Coverage of the brief's 36 tests

### 7.1 Mapping to layers and milestones
| Group | IDs | Mode | Needs | Milestone |
|---|---|---|---|---|
| A Functional | A1–A3 | vector + memory-emulation | reset, BIU, BHE#/A0 | M5 |
| | A4–A7 | memory-emulation | INTR/INTA, NMI edge, HOLD/HLDA float, HLT | M8 |
| B Timing | B1–B6 | memory-emulation + edge search | ElectricalModel derating, setup sampling, READY/Tw, `measure_time` | M6 |
| C Electrical | C1–C7 | PMU + functional at corners | ElectricalModel levels, leakage, ICC, VCC/temperature corners | M6 |
| D Instruction | D1–D11 | memory-emulation vs golden | EU subset, flags, strings, TF, divide error | M5 (D1–D6, D8, D9, D11), M8 (D7, D10) |
| E Max mode | E1–E5 | memory-emulation | S2–S0, QS, LOCK#, RQ#/GT#, TEST#/WAIT | M9 |

### 7.2 Simulation caveats worth stating in the report
- C-group results exercise the *test methodology* against a synthetic ElectricalModel. They say nothing about real silicon.
- E1 checks the S2–S0 decode directly. Modeling the 8288 is out of scope; the checker applies the decode table instead.
- D11 encodes the brief's point that the 8086 does **not** trap invalid opcodes. The golden and the model must both treat `0Fh` as POP CS and `D6h` as undocumented, without raising type 6.

### 7.3 Instruction subset for v1
MOV (reg, imm, mem, sreg), PUSH/POP, PUSHF/POPF, ADD/ADC/SUB/CMP/INC/DEC, AND/OR/XOR, SHL/SHR with CL, MOVS/STOS with REP, CLD/STD/STC/CLC/CLI/STI, JMP/Jcc/CALL/RET (near and far), INT/IRET, DIV, OUT/IN, HLT, NOP, and segment-override prefixes. Everything else raises a model `Unimplemented` error, which reports as **Error**, not Fail.

---

## 8. Build, unit tests, regression

- **CMake ≥ 3.24, C++20**, `CMakePresets.json` with `dev`, `asan`, and `release` presets. Dependencies come through `FetchContent`: yaml-cpp and a unit-test framework (GoogleTest proposed). NASM is found with `find_program` to assemble the ROMs.
- **Targets:** `i86spec`, `i86model`, `i86tester`, `i86golden`, `i86testprog` (libraries); `i86test` (runner CLI); `*_unit` test executables per library.
- **Unit tests per layer, owned by the layer owner:**
  - spec: schema validity, expression evaluation, and a golden table of evaluated limits per grade.
  - model: T-state sequences for read, write, fetch, odd-word access, and Tw insertion; RESET state; per-instruction white-box tests; each fault hook.
  - tester: level and abs-max guards, the timing-set edge math, the pattern parser round-trip, and `search_pass_fail` (against a fake `DeviceModel`).
  - golden: per-instruction flag tables (D1, D2, D8 vectors), plus a cross-check against the model on random instruction streams (differential fuzz, fixed seed).
  - testprog: judge, datalog schema, and shmoo writer.
- **One-command regression: `./regress`**, which runs `cmake --workflow --preset regress`:
  1. Configure, build, and run `ctest` (all unit tests).
  2. Run all 36 tests × 3 grades × VCC {4.5, 5.0, 5.5} × TA {0, 70} on `dies/good.yaml`. All must pass, apart from waivers listed in `regression/expected.yaml`.
  3. Run each `dies/fault_*.yaml`. The tests listed as catching it in `expected.yaml` must fail (this is the fault-coverage check).
  4. Generate the shmoo for `good` and `marginal` and compare it cell by cell with `regression/baselines/*.csv`.
  5. Exit non-zero on any difference from the baseline. Print a short diff and write `out/summary.md`.
- **Baseline update:** `./regress --accept`, run deliberately and reviewed in the PR (the same pattern as golden-file tests).
- **CI:** the same `./regress` on every PR, with `out/` uploaded as an artifact.

---

## 9. Proposed repo layout
```
i8086-eda/
├── CMakeLists.txt  CMakePresets.json  regress            # regress = one-command script
├── spec/
│   ├── spec.yaml                       # L1 owner
│   ├── schema/spec.schema.json
│   └── dies/ good.yaml marginal.yaml fault_stuck_ad3.yaml fault_slow_rd.yaml fault_lowvcc.yaml ...
├── libs/
│   ├── spec/    include/i8086/spec/   src/  tests/       # L1
│   ├── model/   include/i8086/model/  src/  tests/       # L2 (behavioral/, fault/, later verilator/)
│   ├── tester/  include/i8086/tester/ src/  tests/       # L3 (sim/, electrical/)
│   └── golden/  include/i8086/golden/ src/  tests/       # L4 (independent emulator)
├── testprog/
│   ├── cases/   A/ B/ C/ D/ E/                           # one .cpp per test ID
│   ├── roms/    boot.asm  d_suite.asm  b1_checksum.asm ...
│   ├── patterns/ reset_vector.pat ...
│   ├── report/  datalog.cpp summary.cpp shmoo.cpp
│   └── cli/     main.cpp
├── regression/  expected.yaml  baselines/
├── third_party/                                          # later: Verilog 8086 core (git submodule)
└── docs/        design.md (this doc)  limits-verification.md
```
**CODEOWNERS** maps each top-level directory to its layer owner, and `include/` headers additionally require the Spec & Integration lead's review. That makes interface changes deliberate.

---

## 10. Milestones (each one is a tracked task)

The interface headers are frozen at M0. After that, M1–M4 run **in parallel** against stubs, which is the core of the collaboration demo.

| ID | Milestone | Deliverable / done when | Owner role | Depends on |
|---|---|---|---|---|
| **M0** | Skeleton and interfaces | Repo, CMake presets, CODEOWNERS, all public headers from §3.3/§4.3/§5.3/§6.1 compiling with stub implementations, `./regress` running (empty) in CI | Spec & Integration lead | — |
| **M1** | Spec v1 | `spec.yaml` covering brief §3, §4.2, and §6 with provenance and `verified` flags; loader, schema, and expression evaluator, with unit tests | Spec & Integration lead | M0 |
| **M2** | Golden emulator | The §7.3 subset, flags-exact; D1/D2/D8 flag tables pass; ROM loader and I/O capture | Test owner (or Golden owner) | M0 |
| **M3** | Device model core (min mode) | T1–T4/Tw/Ti, prefetch queue, RESET, odd-word split, the §7.3 subset; T-state unit tests; good-die profile | Model owner | M0, M1 (pins) |
| **M4** | Tester API and SimTester | Levels and timing sets, pattern parser, vector mode, memory-emulation mode, BusTrace, `search_pass_fail`; runs against a fake model and then the real one | Tester owner | M0, M1 |
| **M5** | Test program v1 | Runner CLI, protocol checker, judge, datalog, summary; tests A1–A3, D1–D6, D8, D9, D11 pass on the good die; model vs golden differential fuzz green | Test owner | M2, M3, M4 |
| **M6** | Timing and electrical margins | ElectricalModel (thresholds, derating, load, leakage, ICC); B1–B6 and C1–C7; shmoo CSV, ASCII, and SVG; marginal-die profile | Tester owner, with Spec lead for limits | M4, M5 |
| **M7** | Fault injection and coverage regression | FaultInjector with every §4.2 fault; `dies/fault_*.yaml`; `regression/expected.yaml`; full `./regress` with baselines | Model owner, with Test owner for expectations | M5, M6 |
| **M8** | Interrupts, HOLD, HLT | INTR/INTA, NMI edge, HOLD/HLDA float, HLT, TF; tests A4–A7, D7, D10 | Model owner, with Test owner | M5 |
| **M9** | Max mode | S2–S0, QS1/QS0, LOCK#, RQ#/GT#, TEST#/WAIT; tests E1–E5 | Model owner, or a 5th teammate | M8 |
| **M10** | Demo ready | §12 demo script rehearsed end to end; seeded "bad change" branch prepared; README quick-start | Spec & Integration lead | M7 (M8/M9 optional) |
| **M11** *(optional)* | Verilator backend | `VerilatorBackend : DeviceModel` around the chosen open-source core; the same regression runs on it; differences documented | Model owner, or a 5th teammate | M7 and decision D1 |
| **X1** *(parallel, anytime)* | Limits verification | Check every `verified: false` value against Intel DS 231455-006; flip the flags; record changes in `docs/limits-verification.md` | Spec & Integration lead | M1 |

**Critical path:** M0 → M1 → (M3 ∥ M4 ∥ M2) → M5 → M6 → M7 → M10.

---

## 11. Teammate split

| Role | Owns layers / dirs | Milestones | 3-person | 4-person (baseline) | 5-person |
|---|---|---|---|---|---|
| **Spec & Integration lead** | L1 `spec/`, `libs/spec`, CMake, CI, `regression/`, interface reviews | M0, M1, M10, X1 | P1 (also takes Tester) | P1 | P1 |
| **Model owner** | L2 `libs/model`, `spec/dies/` | M3, M7, M8, (M9, M11) | P2 | P2 | P2 (M3, M7, M8) |
| **Tester owner** | L3 `libs/tester` | M4, M6 | P1 | P3 | P3 |
| **Test owner** | L4 `testprog/`, `libs/golden` | M2, M5, test side of M6–M9 | P3 | P4 | P4 (M5, cases, reporting) |
| **5th seat: Golden and System** | `libs/golden`, max mode, Verilator | M2, M9, M11 | — | — | P5 |

Rules that keep the parallel work safe:
1. Public headers change only through a PR reviewed by the Spec & Integration lead and the affected owners.
2. Each owner keeps a fake or stub of the layer below for their own unit tests.
3. The golden emulator and the device model never share execute code (to avoid common-mode bugs).
4. Nobody edits `regression/baselines/` without `./regress --accept` and a reason in the PR.

---

## 12. Demo script — "the regression catches a teammate"

About 10 minutes, with each teammate (and their Cursor agent) in the same Cursor Project:

1. **Kickoff (Spec lead).** Shows the Project: this doc and the brief in the shared store, and M0–M10 as tracked tasks with owners. Opens the green `./regress` summary and the good-die shmoo.
2. **Parallel work (all).** Each owner's agent shows the task it is on (for example, the Tester owner's agent adding CL-dependent delay for B5). It works only in its own directory against frozen headers.
3. **The bad change (Model owner).** A plausible "optimization" PR in the model: the RD# deassert is moved one half-clock earlier "to speed up reads", and a refactor of the INC flag logic accidentally clears CF.
4. **Regression catches it (CI, ~1 min).**
   - `B5 TRLRH FAIL 8086-1 @ 4.5 V: 158.0 ns < min 160.0 ns` (limit `2*TCLCL-40` at TCLCL = 100 ns).
   - `D2 FAIL: CF expected 1 (golden), got 0 (model)`, with the instruction trace diff.
   - Shmoo diff: the 8086-1 spec box now has fail cells at low VCC.
5. **Triage across owners.** The Test owner's agent reads the datalog and points to the model commit. The Model owner's agent reverts the INC change. They discuss the RD# change: it is a real spec violation, so it is rejected.
6. **A legitimate cross-layer change (Spec lead).** Limits verification (X1) finds that TDVCL for the 8086-1 is different from the brief's "(verify)" value. Only `spec.yaml` changes. B3's pass criterion and the pattern strobe edges update automatically, and the regression shows the new margin. The point: one source of truth.
7. **Fault coverage (Model owner).** `fault_stuck_ad3.yaml` makes A1, A3, and the D-suite fail exactly as `expected.yaml` predicts, so the coverage table stays all green.
8. **Close.** The coordinator updates the tracked tasks; summary.md and the shmoo SVG are attached to the PR.

---

## 13. Open decisions

| # | Decision | Options | Recommendation |
|---|---|---|---|
| D1 | **Device model backend** *(open with the user)* | C++ behavioral only · Verilator on an open-source Verilog 8086 core only · hybrid | **Hybrid: C++ first (M3), Verilator later as optional M11** |
| D2 | Which Verilog core, if D1 = hybrid | Survey open-source 8086 cores for license, min/max mode, and bus-cycle accuracy | Decide at the start of M11, not before |
| D3 | Team size | 3 / 4 / 5 | 4 (baseline in §11) |
| D4 | Unit-test framework | GoogleTest · Catch2 | GoogleTest |
| D5 | Spec format and parser | YAML (yaml-cpp) · JSON (nlohmann) · YAML converted to JSON at build time | YAML + yaml-cpp |
| D6 | Pattern format | Custom `.pat` (§6.2) · a STIL subset | Custom `.pat` (simpler), with STIL export later if wanted |
| D7 | Test ROM toolchain | NASM `cpu 8086` · hand-assembled bytes · our own mini-assembler | NASM |
| D8 | Max mode (E-group) in demo scope? | In (M9) · stretch | Stretch; the demo works without it |
| D9 | Shmoo rendering | Native SVG · Python/matplotlib | Native SVG + ASCII |
| D10 | Where the repo lives, and the CI provider | — | Needs the user's input |
| D11 | Limits verification owner and timing | Before M5 · in parallel (X1) | In parallel. Results carry `verified` flags until it is done. |

## 14. Risks

| Risk | Impact | Mitigation |
|---|---|---|
| Many spec values in the brief are **unverified** (the datasheet could not be retrieved) | Wrong pass/fail limits | `verified` flag per limit, report banner, task X1 |
| Golden and model share a misreading of the ISA (common-mode bug) | Bugs pass silently | Separate owners and no shared code; brief-derived expected values in D-tests; differential fuzz |
| A synthetic ElectricalModel makes margins look more meaningful than they are | Misleading demo claims | §7.2 caveats printed in the report; message the demo as methodology |
| Interface churn across 4 owners | Blocked parallel work | Freeze at M0, CODEOWNERS on `include/`, stubs per layer |
| ISA scope creep (full 8086) | Milestone slip | Fixed v1 subset (§7.3); `Unimplemented` reports as Error, not Fail |
| Regression too slow once shmoo and the full matrix run | People stop running it | Short ROMs, parallel test execution, a coarse shmoo grid in `regress` and a fine one on demand |
| Verilog core not cycle-accurate or awkwardly licensed | M11 slips or diverges | Keep M11 optional; document differences rather than block |
| Demo depends on a seeded bug behaving the same every time | Live-demo failure | Deterministic ps time base and fixed seeds; prepared branch rehearsed in M10 |
