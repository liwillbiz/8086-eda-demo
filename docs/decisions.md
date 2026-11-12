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
