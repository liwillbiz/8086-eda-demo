#include "timing.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>

#include "../timing_expr.hpp"

namespace i8086::tester::detail {
namespace {

std::string fmt_ns(double ns) {
  std::string s = std::to_string(ns);
  s.erase(s.find_last_not_of('0') + 1);
  if (s.back() == '.') s.pop_back();
  return s + " ns";
}

DriveFormat parse_fmt(const std::string& s) {
  if (s == "NRZ") return DriveFormat::NRZ;
  if (s == "RZ") return DriveFormat::RZ;
  if (s == "R1") return DriveFormat::R1;
  throw std::invalid_argument("unknown drive format '" + s + "' (use NRZ, RZ or R1)");
}

const PinTiming* find_pin_timing(const TimingSet& ts, const spec::Pin& p) {
  if (auto it = ts.pins.find(p.name); it != ts.pins.end()) return &it->second;
  if (p.bus) {
    if (auto it = ts.pins.find(*p.bus); it != ts.pins.end()) return &it->second;
  }
  return nullptr;
}

void check_edge(const std::string& what, int64_t ps, int64_t period_ps) {
  if (ps < 0 || ps >= period_ps) {
    throw std::invalid_argument(what + " = " + fmt_ns(static_cast<double>(ps) / 1000) +
                                " is outside the period [0, " +
                                fmt_ns(static_cast<double>(period_ps) / 1000) + ")");
  }
}

}  // namespace

int64_t to_ps(double ns) {
  if (!std::isfinite(ns)) throw std::invalid_argument("time is not finite");
  return std::llround(ns * 1000);
}

double default_clk_high_ns(const spec::Spec& spec, double period_ns) {
  double lo = 0, hi = period_ns;
  for (spec::Grade g : spec::kAllGrades) {
    if (auto m = spec.ac("TCHCL", g, period_ns).min) lo = std::max(lo, *m);
    if (auto m = spec.ac("TCLCH", g, period_ns).min) hi = std::min(hi, period_ns - *m);
  }
  return lo <= hi ? (lo + hi) / 2 : period_ns / 3;
}

void validate_timing_set(const spec::Spec& spec, const TimingSet& ts) {
  if (!std::isfinite(ts.period_ns) || ts.period_ns <= 0) {
    throw std::invalid_argument("timing set: period must be > 0 ns");
  }
  const int64_t period_ps = to_ps(ts.period_ns);
  if (ts.clk_high_ns != 0) check_edge("clk_high", to_ps(ts.clk_high_ns), period_ps);
  for (const auto& [name, pt] : ts.pins) {
    const bool is_bus = std::ranges::any_of(spec.pins(), [&](const spec::Pin& p) { return p.bus == name; });
    if (!is_bus) {
      const spec::Pin& p = spec.pin(name);
      if (p.dir == spec::Dir::Pwr || p.name == "CLK") {
        throw std::invalid_argument("timing set: pin " + name + " has no drive/strobe timing");
      }
    }
    check_edge("timing set: " + name + " drive", to_ps(pt.drive_ns), period_ps);
    check_edge("timing set: " + name + " strobe", to_ps(pt.strobe_ns), period_ps);
  }
}

ResolvedTiming resolve_timing(const spec::Spec& spec, const TimingSet& base,
                              const PatternTiming* overlay) {
  const auto attr = [&](const std::string& key) -> const std::string* {
    if (!overlay) return nullptr;
    const auto it = overlay->attrs.find(key);
    return it == overlay->attrs.end() ? nullptr : &it->second;
  };
  const std::string where = overlay ? "timing " + std::to_string(overlay->id) + ": " : "";

  std::optional<spec::Grade> grade;
  if (const auto* g = attr("grade")) grade = spec::parse_grade(*g);

  TimingExprEnv env;
  env.symbol = [&](const SymbolRef& s) -> double {
    if (!grade) {
      throw TimingExprError(s.name + (s.max ? ".max" : ".min") +
                            " needs a grade=<8086|8086-2|8086-1> timing attribute");
    }
    const spec::Limit lim = spec.ac(s.name, *grade, *env.tclcl_ns);
    const auto& bound = s.max ? lim.max : lim.min;
    if (!bound) throw TimingExprError(s.name + " has no " + (s.max ? "max" : "min") + " bound");
    return *bound;
  };
  const auto eval = [&](const std::string& key, const std::string& text) {
    try {
      return eval_timing_expr(text, env);
    } catch (const std::exception& e) {
      throw std::invalid_argument(where + key + ": " + e.what());
    }
  };

  ResolvedTiming rt;
  env.tclcl_ns = base.period_ns;
  rt.period_ns = base.period_ns;
  if (const auto* p = attr("period")) rt.period_ns = eval("period", *p);
  if (!(rt.period_ns > 0)) throw std::invalid_argument(where + "period must be > 0 ns");
  rt.period_ps = to_ps(rt.period_ns);
  env.tclcl_ns = rt.period_ns;

  double high_ns = base.clk_high_ns;
  if (const auto* h = attr("clk_high")) high_ns = eval("clk_high", *h);
  if (high_ns == 0) high_ns = default_clk_high_ns(spec, rt.period_ns);
  rt.high_ps = to_ps(high_ns);
  if (rt.high_ps <= 0 || rt.high_ps >= rt.period_ps) {
    throw std::invalid_argument(where + "clk_high must be inside (0, period)");
  }

  std::optional<int64_t> g_drive, g_strobe;
  std::optional<DriveFormat> g_fmt;
  if (const auto* d = attr("drive")) g_drive = to_ps(eval("drive", *d));
  if (const auto* s = attr("strobe")) g_strobe = to_ps(eval("strobe", *s));
  if (const auto* f = attr("fmt")) g_fmt = parse_fmt(*f);

  int max_num = 0;
  for (const auto& p : spec.pins()) max_num = std::max(max_num, p.num);
  rt.pins.resize(static_cast<std::size_t>(max_num) + 1);
  for (const auto& p : spec.pins()) {
    PinEdges& e = rt.pins[static_cast<std::size_t>(p.num)];
    if (const PinTiming* pt = find_pin_timing(base, p)) {
      e = {pt->fmt, to_ps(pt->drive_ns), to_ps(pt->strobe_ns)};
    }
    if (g_drive) e.drive_ps = *g_drive;
    if (g_strobe) e.strobe_ps = *g_strobe;
    if (g_fmt) e.fmt = *g_fmt;
    if (const auto* d = attr("drive." + p.name)) e.drive_ps = to_ps(eval("drive." + p.name, *d));
    if (const auto* s = attr("strobe." + p.name)) e.strobe_ps = to_ps(eval("strobe." + p.name, *s));
    if (const auto* f = attr("fmt." + p.name)) e.fmt = parse_fmt(*f);
    if (p.dir == spec::Dir::Pwr || p.name == "CLK") continue;
    check_edge(where + p.name + " drive", e.drive_ps, rt.period_ps);
    check_edge(where + p.name + " strobe", e.strobe_ps, rt.period_ps);
  }
  return rt;
}

}  // namespace i8086::tester::detail
