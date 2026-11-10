#include "coverage.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <format>

#include "i8086/model/die_profile.hpp"
#include "i8086/spec/spec.hpp"

namespace i8086::testprog {
namespace {

int rank(Verdict v) {
  switch (v) {
    case Verdict::Pass: return 0;
    case Verdict::Skip: return 1;
    case Verdict::Fail: return 2;
    case Verdict::Error: return 3;
  }
  return 0;
}

bool caught(Verdict v) { return v == Verdict::Fail || v == Verdict::Error; }

std::string join(const std::set<std::string>& s, std::string_view sep = " ") {
  std::string out;
  for (const auto& x : s) out += (out.empty() ? "" : std::string(sep)) + x;
  return out;
}

std::string describe(const model::Fault& f) {
  struct V {
    std::string operator()(const model::StuckAt& x) const {
      return std::format("StuckAt {}={}", x.pin, x.value) +
             (x.start_clk ? std::format(" from CLK {}", *x.start_clk) : "");
    }
    std::string operator()(const model::SlowTiming& x) const {
      std::string p;
      for (const auto& s : x.params) p += (p.empty() ? "" : ",") + s;
      std::string out = "SlowTiming " + (p.empty() ? std::string("all") : p);
      if (x.scale != 1.0) out += std::format(" x{:g}", x.scale);
      if (x.add_ns != 0.0) out += std::format(" +{:g} ns", x.add_ns);
      return out;
    }
    std::string operator()(const model::SetupDegrade& x) const {
      static constexpr const char* kNames[] = {"data", "READY", "INTR/NMI/HOLD"};
      return std::format("SetupDegrade {} +{:g} ns", kNames[static_cast<int>(x.input)], x.add_ns);
    }
    std::string operator()(const model::LowVccFail& x) const {
      return std::format("LowVccFail below {:g} V", x.vcc_floor_v);
    }
    std::string operator()(const model::Leaky& x) const {
      return std::format("Leaky {} {:g} uA", x.pin, x.leakage_ua);
    }
    std::string operator()(const model::LogicBug& x) const { return "LogicBug " + x.name; }
  };
  return std::visit(V{}, f);
}

}  // namespace

Expected load_expected(const std::filesystem::path& path) {
  Expected e;
  try {
    const YAML::Node root = YAML::LoadFile(path.string());
    const YAML::Node fc = root["fault_coverage"];
    if (fc && !fc.IsMap()) throw spec::SpecError("fault_coverage must be a map", path.string(), fc.Mark().line + 1);
    for (const auto& kv : fc) {
      auto& tests = e.fault_coverage[kv.first.as<std::string>()];
      for (const auto& t : kv.second) tests.insert(t.as<std::string>());
    }
  } catch (const YAML::BadFile&) {
    throw spec::SpecError("cannot open expectations", path.string(), 0);
  } catch (const YAML::Exception& ex) {
    throw spec::SpecError(ex.msg, path.string(), ex.mark.line + 1);
  }
  return e;
}

std::set<std::string> DieCoverage::missing() const {
  std::set<std::string> out;
  std::ranges::set_difference(expected, caught, std::inserter(out, out.end()));
  return out;
}

std::set<std::string> DieCoverage::unexpected() const {
  std::set<std::string> out;
  std::ranges::set_difference(caught, expected, std::inserter(out, out.end()));
  return out;
}

bool CoverageResult::ok() const {
  return std::ranges::all_of(dies, &DieCoverage::ok);
}

CoverageResult run_coverage(const CoverageConfig& cfg, std::ostream& console) {
  const Expected expected = load_expected(cfg.expected_path);
  std::set<std::string> names;
  for (const auto& entry : std::filesystem::directory_iterator(cfg.dies_dir)) {
    const std::string stem = entry.path().stem().string();
    if (entry.path().extension() == ".yaml" && stem.starts_with("fault_")) names.insert(stem);
  }
  for (const auto& [die, tests] : expected.fault_coverage) names.insert(die);

  CoverageResult result;
  result.tests = TestRegistry::global().select(cfg.run.tests);
  std::ostream quiet(nullptr);
  for (const auto& name : names) {
    DieCoverage dc;
    dc.die = name;
    if (const auto it = expected.fault_coverage.find(name); it != expected.fault_coverage.end()) {
      dc.expected = it->second;
    }
    RunConfig rc = cfg.run;
    rc.die_path = cfg.dies_dir / (name + ".yaml");
    rc.out_dir = cfg.run.out_dir / name;
    if (!std::filesystem::exists(rc.die_path)) {
      dc.has_die = false;
    } else {
      const model::DieProfile die = model::DieProfile::load(rc.die_path);
      for (const auto& f : die.faults.faults) dc.faults += (dc.faults.empty() ? "" : "; ") + describe(f);
      for (const ResultRow& row : run(rc, TestRegistry::global(), quiet).rows) {
        auto [it, inserted] = dc.worst.try_emplace(row.test, row.verdict);
        if (!inserted && rank(row.verdict) > rank(it->second)) it->second = row.verdict;
        if (caught(row.verdict)) dc.caught.insert(row.test);
      }
    }
    console << std::format("   {:<26} {:<4} caught by: {}", name, dc.ok() ? "ok" : "FAIL", join(dc.caught));
    if (!dc.has_die) console << " (no " << rc.die_path.string() << ")";
    if (!dc.missing().empty()) console << " | missed: " << join(dc.missing());
    if (!dc.unexpected().empty()) console << " | unexpected: " << join(dc.unexpected());
    if (dc.has_die && !expected.fault_coverage.contains(name)) console << " | not in expected.yaml";
    console << "\n";
    result.dies.push_back(std::move(dc));
  }
  return result;
}

void write_coverage_csv(std::ostream& os, const CoverageResult& r) {
  os << "die,test,expected,caught,worst_verdict\n";
  for (const auto& d : r.dies) {
    for (const auto& t : r.tests) {
      const auto it = d.worst.find(t);
      os << d.die << ',' << t << ',' << (d.expected.contains(t) ? 1 : 0) << ',' << (d.caught.contains(t) ? 1 : 0)
         << ',' << (it == d.worst.end() ? "" : std::string(to_string(it->second))) << '\n';
    }
  }
}

void write_coverage_md(std::ostream& os, const CoverageResult& r) {
  int ok = 0;
  for (const auto& d : r.dies) ok += d.ok() ? 1 : 0;
  os << "## Fault coverage\n\n"
     << std::format("{} of {} fault dies caught exactly as `regression/expected.yaml` predicts.\n\n", ok,
                    r.dies.size())
     << "| Die | Fault | Caught by | Status |\n|---|---|---|---|\n";
  for (const auto& d : r.dies) {
    std::string status = d.ok() ? "ok" : "**MISMATCH**";
    if (!d.has_die) status += " (die file missing)";
    if (!d.missing().empty()) status += "; missed " + join(d.missing(), ", ");
    if (!d.unexpected().empty()) status += "; unexpected " + join(d.unexpected(), ", ");
    os << "| " << d.die << " | " << d.faults << " | " << join(d.caught, ", ") << " | " << status << " |\n";
  }
  os << "\nMatrix: `F` fail, `E` error (counts as caught), `.` pass; `!` marks a cell that "
        "differs from expected.yaml.\n\n| Die |";
  for (const auto& t : r.tests) os << ' ' << t << " |";
  os << "\n|---|";
  for (std::size_t i = 0; i < r.tests.size(); ++i) os << ":-:|";
  os << '\n';
  for (const auto& d : r.dies) {
    os << "| " << d.die << " |";
    for (const auto& t : r.tests) {
      const auto it = d.worst.find(t);
      const Verdict v = it == d.worst.end() ? Verdict::Pass : it->second;
      std::string cell = v == Verdict::Fail ? "F" : v == Verdict::Error ? "E" : ".";
      if (d.expected.contains(t) != d.caught.contains(t)) cell += "!";
      os << ' ' << cell << " |";
    }
    os << '\n';
  }
}

}  // namespace i8086::testprog
