# 8086-eda-demo

A small, fully simulated "chip tester" for the Intel 8086. It demonstrates a realistic
ATE-style test-system architecture and a four-person parallel workflow in Cursor Projects.
Every analog quantity is synthetic: this is a test *methodology* demo, not silicon
characterization.

- Design: [`docs/design.md`](docs/design.md)
- Device facts: [`docs/8086-reference-brief.md`](docs/8086-reference-brief.md)
- Decisions: [`docs/decisions.md`](docs/decisions.md)

**Status: M0 (skeleton and frozen interfaces).** Every public header compiles, and the
stub implementations throw `NotImplemented`, which the runner reports as **Error**.
`./regress` runs green end to end with no test cases registered yet.

## The four layers

```
spec/spec.yaml ── read by all
     │
testprog (L4) ──TestCase API──▶ Tester (L3, abstract ATE) ──▶ SimTester + ElectricalModel
  cases, golden, datalog, report                                      │ DeviceModel API
                                                                      ▼
                                                    Device model (L2) + FaultInjector
```

| # | Layer | Headers | Owner (CODEOWNERS) |
|---|---|---|---|
| 1 | **Spec**: datasheet limits as data (pins, AC per grade, DC, abs max, decode tables), each with provenance and a `verified` flag | `libs/spec/include/i8086/spec/spec.hpp` | Spec & Integration lead (P1) |
| 2 | **Device model**: behavioral C++ 8086 at bus-cycle level, plus fault injection. Reads `spec/dies/*.yaml` ("simulated silicon"). | `libs/model/include/i8086/model/{device_model,fault,fault_injector,die_profile}.hpp` | Model owner (P2) |
| 3 | **Tester**: abstract ATE API (levels, timing sets, patterns, memory emulation, PMU, edge search). `SimTester` wraps the model; `ElectricalModel` adds the synthetic analog margins. | `libs/tester/include/i8086/tester/*.hpp` | Tester owner (P3) |
| 4 | **Test program**: the brief's 36 tests as `TestCase`s, an independent golden emulator, datalog, summary, and the `i86test` runner | `testprog/include/i8086/testprog/*.hpp`, `libs/golden/include/i8086/golden/emulator.hpp` | Test owner (P4) |

The dependency rule is that a layer uses only the layers below it and `spec`. Test cases see
only `tester::Tester`, never `DeviceModel`. The golden emulator links nothing from the device
model, so the two cannot share a bug. Limits live only in `spec/spec.yaml`, while what a
particular die actually does lives in `spec/dies/`. The tests know only the spec, so they have
to *discover* how a die differs.

## Build and run

Requirements: CMake ≥ 3.25, a C++20 compiler (GCC ≥ 13 or Clang ≥ 17), and network access on
the first configure (FetchContent downloads yaml-cpp and GoogleTest).

```bash
./regress                  # configure + build + unit tests + test matrix + baseline diff
./regress --accept         # same, then update regression/baselines/ (review the diff in the PR)

cmake --workflow --preset dev          # debug build + unit tests only
cmake --preset asan && cmake --build --preset asan && ctest --preset asan

build/regress/testprog/i86test --list
build/regress/testprog/i86test --die spec/dies/good.yaml --grade 8086-2 \
    --vcc 4.5,5.0,5.5 --temp 0,70 --tests 'A*,D1-D11' --out out/
```

Outputs are written to `out/`: `summary.md`, `results.csv`, and `datalog.jsonl`. Presets:
`dev`, `asan`, `release`, and `regress` (RelWithDebInfo with `-Werror`; this is what CI runs).
To pick a compiler, set `CXX`, for example `CXX=g++ ./regress`.

## Repo layout

```
spec/            spec.yaml, schema/, dies/            L1 (dies/: L2)
libs/common/     header-only shared error types        L1
libs/spec/       loader, validator, expressions        L1
libs/model/      behavioral/, fault/, die profiles     L2
libs/tester/     Tester API, sim/, electrical/         L3
libs/golden/     independent instruction emulator      L4
testprog/        include/, src/ (registry, runner), cases/A..E, roms/, patterns/, report/, cli/
regression/      expected.yaml, baselines/
docs/            design, brief, decisions
```

Each library has its own `tests/`, built as a `*_unit` GoogleTest executable and owned by
that layer's owner.

## Working in parallel

The interfaces were frozen at M0. After that, M1 (spec), M2 (golden), M3 (model), and
M4 (tester) proceed in parallel against the stubs. Changes to any `include/` header need a
review from the Spec & Integration lead and the owner of that layer (see `.github/CODEOWNERS`
and design.md §11). To add a test case, add `testprog/cases/<group>/<ID>.cpp` with a
`TestCase` subclass and `REGISTER_TEST(ClassName);`.
