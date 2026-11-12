# 8086-eda-demo

A small, fully simulated "chip tester" for the Intel 8086. It demonstrates a realistic
ATE-style test-system architecture and a four-person parallel workflow in Cursor Projects.
Every analog quantity is synthetic: this is a test *methodology* demo, not silicon
characterization.

- Design: [`docs/design.md`](docs/design.md)
- Device facts: [`docs/8086-reference-brief.md`](docs/8086-reference-brief.md)
- Decisions: [`docs/decisions.md`](docs/decisions.md)

**Status: M10 (demo ready).** M5 (A1–A3, D1–D6, D8, D9, D11 with golden diff and
differential fuzz), M6 (B1–B5 and C1–C7 margins, shmoo), M7 (fault injection and coverage) and
M8 (INTR/INTA, NMI, HOLD/HLDA, HLT, interruptible REP; A4–A7, B6, D7, D10) are in. Each of the
12 `spec/dies/fault_*.yaml` dies must be caught by exactly the tests `regression/expected.yaml`
lists for it. `spec/dies/good.yaml` and `spec/dies/marginal.yaml` pass every test at every
grade × VCC × TA point. `./regress` diffs the results matrix and both dies' shmoos against
`regression/baselines/`. Max mode (M9) is a stretch goal.

## Demo quick start

The demo (design doc §12) uses `demo/demo.sh`; run it with no arguments for help.

```bash
export DEMO_BASE=origin/main            # the branch demo PRs target
demo/demo.sh check                      # tools, git identity, base branch, patches
demo/demo.sh kickoff                    # ./regress, green summary and good-die shmoo
demo/demo.sh branch risky --push        # RD# "speed-up" + INC refactor -> open the PR it links
demo/demo.sh explain                    # after ./regress (or in an unzipped CI artifact)
demo/demo.sh revert inc --push          # triage: B5 is the only failure left
demo/demo.sh revert speedup --push      # green again
demo/demo.sh branch spec-fix --value 5 --push   # one line in spec/spec.yaml (TDVCL, 8086-1)
demo/demo.sh stuck-pin                  # fault_stuck_ad3 fails exactly as expected.yaml says
```

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

Requirements: CMake ≥ 3.25, a C++20 compiler (GCC ≥ 13 or Clang ≥ 17), NASM (assembles the
test ROMs; `apt install nasm` / `brew install nasm`), and network access on the first configure
(FetchContent downloads yaml-cpp and GoogleTest).

```bash
./regress                  # build + unit tests + test matrix + fault coverage + shmoo + baseline diff
./regress --accept         # same, then update regression/baselines/ (review the diff in the PR)

cmake --workflow --preset dev          # debug build + unit tests only
cmake --preset asan && cmake --build --preset asan && ctest --preset asan

build/regress/testprog/i86test --list
build/regress/testprog/i86test --shmoo --die spec/dies/marginal.yaml --out out/shmoo
build/regress/testprog/i86test --coverage regression/expected.yaml --vcc 4.5,5.5 --temp 70 --out out/faults
build/regress/testprog/i86test --die spec/dies/good.yaml --grade 8086-2 \
    --vcc 4.5,5.0,5.5 --temp 0,70 --tests 'A*,D1-D11' --out out/
```

Outputs are written to `out/`: `summary.md`, `results.csv`, and `datalog.jsonl`. Presets:
`dev`, `asan`, `release`, and `regress` (RelWithDebInfo with `-Werror`; this is what CI runs).
To pick a compiler, set `CXX`, for example `CXX=g++ ./regress`. `I86_JOBS=N` sets the
matrix's thread count (default: one per core).

## Repo layout

```
spec/            spec.yaml, schema/, dies/            L1 (dies/: L2)
libs/common/     header-only shared error types        L1
libs/spec/       loader, validator, expressions        L1
libs/model/      behavioral/, fault/, die profiles     L2
libs/tester/     Tester API, sim/, electrical/         L3
libs/golden/     independent instruction emulator      L4
testprog/        include/, src/ (registry, runner, ROM and margin harness, protocol checker,
                 shmoo), cases/A..E,
                 roms/ (NASM test ROMs), patterns/ (.pat), report/, cli/
regression/      expected.yaml, baselines/
demo/            demo.sh, patches/ (the prepared risky change)
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
