#include "i8086/spec/spec.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include "expr.hpp"

namespace i8086::spec {

namespace detail {

// One grade's bounds for an AC or DC parameter, before evaluation.
struct BoundSpec {
  std::optional<Expr> min, max;
  std::string ref;
  bool verified = false;
};

struct Param {
  std::string unit;
  std::map<Grade, BoundSpec> grades;
  int line = 0;
};

struct Provenance {
  std::string ref;
  bool verified = false;
};

}  // namespace detail

using detail::Bound;
using detail::BoundSpec;
using detail::Expr;
using detail::ExprError;
using detail::Param;
using detail::Var;

struct Spec::Impl {
  std::filesystem::path path;
  int schema_version = 0;
  std::string device_name;
  std::map<Grade, GradeInfo> grades;
  std::map<Grade, detail::Provenance> grade_prov;
  std::vector<Pin> pins;
  std::map<std::string, Limit, std::less<>> conditions;
  std::map<std::string, Limit, std::less<>> abs_max;
  std::map<std::string, Param, std::less<>> ac;
  std::map<std::string, Param, std::less<>> dc;
  DecodeTables decode;

  Limit eval_ac(std::string_view sym, Grade, double tclcl_ns, int depth = 0) const;
  Limit eval_dc(std::string_view sym, Grade) const;

  [[noreturn]] void fail(const std::string& msg) const { throw SpecError(msg, path.string()); }
};

SpecError::SpecError(const std::string& msg, std::string file, int line)
    : std::runtime_error(file.empty() ? msg
                                      : file + ":" + std::to_string(line) + ": " + msg),
      file_(std::move(file)),
      line_(line) {}

std::string_view to_string(Grade g) {
  switch (g) {
    case Grade::G8086: return "8086";
    case Grade::G8086_2: return "8086-2";
    case Grade::G8086_1: return "8086-1";
  }
  return "?";
}

Grade parse_grade(std::string_view s) {
  for (Grade g : kAllGrades) {
    if (to_string(g) == s) return g;
  }
  throw SpecError("unknown speed grade '" + std::string(s) + "'");
}

// ---------------------------------------------------------------------------------------------
// Evaluation

namespace {

constexpr int kMaxRefDepth = 16;

std::string var_text(const Var& v) {
  if (!v.bound) return v.name;
  return v.name + (*v.bound == Bound::Min ? ".min" : ".max");
}

}  // namespace

Limit Spec::Impl::eval_ac(std::string_view sym, Grade g, double tclcl_ns, int depth) const {
  if (depth > kMaxRefDepth) fail("AC reference cycle through '" + std::string(sym) + "'");
  if (sym == "TCLCL") {
    const GradeInfo& gi = grades.at(g);
    const auto& prov = grade_prov.at(g);
    return Limit{gi.tclcl_min_ns, gi.tclcl_max_ns, "ns", prov.ref, prov.verified};
  }
  const auto it = ac.find(sym);
  if (it == ac.end()) fail("unknown AC parameter '" + std::string(sym) + "'");
  const BoundSpec& b = it->second.grades.at(g);

  Limit out;
  out.unit = it->second.unit;
  out.ref = b.ref;
  out.verified = b.verified;
  const Expr::Resolver resolve = [&](const Var& v) -> double {
    if (!v.bound) {
      if (v.name == "TCLCL") return tclcl_ns;
      fail("AC parameter '" + std::string(sym) + "': unknown variable '" + v.name + "'");
    }
    const Limit dep = eval_ac(v.name, g, tclcl_ns, depth + 1);
    out.verified = out.verified && dep.verified;
    const auto& val = *v.bound == Bound::Min ? dep.min : dep.max;
    if (!val) {
      fail("AC parameter '" + std::string(sym) + "' uses " + var_text(v) + ", which has no " +
           (*v.bound == Bound::Min ? "min" : "max") + " for grade " +
           std::string(to_string(g)));
    }
    return *val;
  };
  if (b.min) out.min = b.min->eval(resolve);
  if (b.max) out.max = b.max->eval(resolve);
  return out;
}

// DC bounds may depend on VCC. The returned limit must hold at every operating VCC, so a
// minimum takes the largest and a maximum the smallest value over the conditions.VCC range.
Limit Spec::Impl::eval_dc(std::string_view sym, Grade g) const {
  const auto it = dc.find(sym);
  if (it == dc.end()) fail("unknown DC parameter '" + std::string(sym) + "'");
  const BoundSpec& b = it->second.grades.at(g);
  const Limit& vcc = conditions.at("VCC");

  Limit out;
  out.unit = it->second.unit;
  out.ref = b.ref;
  out.verified = b.verified;
  auto at_vcc = [&](const Expr& e, double v) {
    return e.eval([&](const Var&) { return v; });
  };
  auto eval = [&](const Expr& e, bool is_min) {
    if (e.is_constant()) return at_vcc(e, 0);
    out.verified = out.verified && vcc.verified;
    const double lo = at_vcc(e, *vcc.min);
    const double hi = at_vcc(e, *vcc.max);
    return is_min ? std::max(lo, hi) : std::min(lo, hi);
  };
  if (b.min) out.min = eval(*b.min, true);
  if (b.max) out.max = eval(*b.max, false);
  return out;
}

// ---------------------------------------------------------------------------------------------
// Loading and validation

namespace {

const std::set<std::string, std::less<>> kUnits{"ns", "V", "mA", "uA", "pF", "C", "W", "MHz"};

class Loader {
 public:
  Loader(Spec::Impl& impl) : impl_(impl), file_(impl.path.string()) {}

  void load(const YAML::Node& root) {
    require_map(root, "spec root");
    check_keys(root, {"schema_version", "device", "grades", "pins", "conditions", "ac", "dc",
                      "abs_max", "decode"});
    impl_.schema_version = static_cast<int>(number(required(root, "schema_version")));
    if (impl_.schema_version != 1) fail(root["schema_version"], "unsupported schema_version");

    const YAML::Node device = required(root, "device");
    require_map(device, "device");
    check_keys(device, {"name", "package", "ref", "verified"});
    impl_.device_name = string(required(device, "name"));
    string(required(device, "package"));

    load_grades(required(root, "grades"));
    load_pins(required(root, "pins"));
    load_flat_section(required(root, "conditions"), "conditions", impl_.conditions, true);
    for (const char* sym : {"VCC", "TA"}) {
      const auto it = impl_.conditions.find(sym);
      if (it == impl_.conditions.end() || !it->second.min || !it->second.max) {
        fail(root["conditions"], std::string("conditions.") + sym + " needs min and max");
      }
    }
    load_flat_section(required(root, "abs_max"), "abs_max", impl_.abs_max, false);
    load_params(required(root, "ac"), "ac", impl_.ac);
    load_params(required(root, "dc"), "dc", impl_.dc);
    load_decode(required(root, "decode"));

    check_ac();
    check_dc();
  }

 private:
  // --- generic helpers -----------------------------------------------------------------------

  [[noreturn]] void fail(const YAML::Node& node, const std::string& msg) const {
    throw SpecError(msg, file_, node.Mark().line + 1);
  }

  void require_map(const YAML::Node& node, const std::string& what) const {
    if (!node.IsMap()) fail(node, what + " must be a mapping");
  }

  YAML::Node required(const YAML::Node& map, const std::string& key) const {
    const YAML::Node n = map[key];
    if (!n) fail(map, "missing required key '" + key + "'");
    return n;
  }

  // Rejects unknown and duplicate keys, so a typo in spec.yaml fails the build.
  void check_keys(const YAML::Node& map, std::initializer_list<std::string_view> allowed) const {
    std::set<std::string> seen;
    for (const auto& kv : map) {
      const auto key = kv.first.as<std::string>();
      if (std::find(allowed.begin(), allowed.end(), key) == allowed.end()) {
        fail(kv.first, "unknown key '" + key + "'");
      }
      if (!seen.insert(key).second) fail(kv.first, "duplicate key '" + key + "'");
    }
  }

  double number(const YAML::Node& node) const {
    if (!node.IsScalar()) fail(node, "expected a number");
    try {
      return node.as<double>();
    } catch (const YAML::Exception&) {
      fail(node, "expected a number, got '" + node.Scalar() + "'");
    }
  }

  std::string string(const YAML::Node& node) const {
    if (!node.IsScalar() || node.Scalar().empty()) fail(node, "expected a non-empty string");
    return node.Scalar();
  }

  bool boolean(const YAML::Node& node) const {
    if (!node.IsScalar()) fail(node, "expected true or false");
    try {
      return node.as<bool>();
    } catch (const YAML::Exception&) {
      fail(node, "expected true or false, got '" + node.Scalar() + "'");
    }
  }

  detail::Provenance provenance(const YAML::Node& node) const {
    return {string(required(node, "ref")), boolean(required(node, "verified"))};
  }

  std::string unit(const YAML::Node& node) const {
    const YAML::Node u = required(node, "unit");
    std::string s = string(u);
    if (!kUnits.contains(s)) fail(u, "unknown unit '" + s + "'");
    return s;
  }

  // --- sections ------------------------------------------------------------------------------

  void load_grades(const YAML::Node& node) {
    require_map(node, "grades");
    check_keys(node, {"8086", "8086-2", "8086-1"});
    for (const auto& kv : node) {
      const Grade g = parse_grade(kv.first.as<std::string>());
      const auto& v = kv.second;
      require_map(v, "grade");
      check_keys(v, {"fclk_max_mhz", "tclcl_ns", "ref", "verified"});
      const YAML::Node t = required(v, "tclcl_ns");
      if (!t.IsSequence() || t.size() != 2) fail(t, "tclcl_ns must be [min, max]");
      GradeInfo gi{number(required(v, "fclk_max_mhz")), number(t[0]), number(t[1])};
      if (!(gi.tclcl_min_ns > 0 && gi.tclcl_min_ns < gi.tclcl_max_ns)) {
        fail(t, "tclcl_ns must satisfy 0 < min < max");
      }
      if (std::abs(1000.0 / gi.tclcl_min_ns - gi.fclk_max_mhz) > 1e-9) {
        fail(v, "fclk_max_mhz does not match 1000 / tclcl_ns[0]");
      }
      impl_.grades[g] = gi;
      impl_.grade_prov[g] = provenance(v);
    }
    for (Grade g : kAllGrades) {
      if (!impl_.grades.contains(g)) fail(node, "missing grade " + std::string(to_string(g)));
    }
  }

  void load_pins(const YAML::Node& node) {
    if (!node.IsSequence()) fail(node, "pins must be a list");
    std::set<int> nums;
    std::set<std::string> names;
    for (const auto& p : node) {
      require_map(p, "pin");
      check_keys(p, {"num", "name", "dir", "tristate", "active_low", "max_mode_name", "bus",
                     "bit", "ref", "verified"});
      Pin pin;
      pin.num = static_cast<int>(number(required(p, "num")));
      if (pin.num < 1 || pin.num > 40) fail(p["num"], "pin number out of range 1..40");
      if (!nums.insert(pin.num).second) fail(p["num"], "duplicate pin number");
      pin.name = string(required(p, "name"));
      pin.dir = parse_dir(required(p, "dir"));
      if (p["tristate"]) pin.tristate = boolean(p["tristate"]);
      if (p["active_low"]) pin.active_low = boolean(p["active_low"]);
      if (p["max_mode_name"]) pin.max_mode_name = string(p["max_mode_name"]);
      if (static_cast<bool>(p["bus"]) != static_cast<bool>(p["bit"])) {
        fail(p, "bus and bit must be given together");
      }
      if (p["bus"]) {
        pin.bus = string(p["bus"]);
        pin.bit = static_cast<int>(number(p["bit"]));
      }
      provenance(p);
      for (const auto& n : {std::optional<std::string>(pin.name), pin.max_mode_name}) {
        if (n && !names.insert(*n).second) fail(p, "duplicate pin name '" + *n + "'");
      }
      impl_.pins.push_back(std::move(pin));
    }
    if (impl_.pins.size() != 40) fail(node, "expected 40 pins");
  }

  Dir parse_dir(const YAML::Node& node) const {
    const auto s = string(node);
    if (s == "in") return Dir::In;
    if (s == "out") return Dir::Out;
    if (s == "io") return Dir::Io;
    if (s == "pwr") return Dir::Pwr;
    fail(node, "invalid pin dir '" + s + "'");
  }

  // conditions / abs_max: numeric min/max (or `value` for conditions), no grades, no formulas.
  void load_flat_section(const YAML::Node& node, const std::string& section,
                         std::map<std::string, Limit, std::less<>>& out, bool allow_value) {
    require_map(node, section);
    std::set<std::string> seen;
    for (const auto& kv : node) {
      const auto sym = kv.first.as<std::string>();
      if (!seen.insert(sym).second) fail(kv.first, "duplicate key '" + sym + "'");
      const auto& v = kv.second;
      require_map(v, section + "." + sym);
      if (allow_value) {
        check_keys(v, {"desc", "unit", "min", "max", "value", "ref", "verified"});
      } else {
        check_keys(v, {"desc", "unit", "min", "max", "ref", "verified"});
      }
      Limit lim;
      lim.unit = unit(v);
      const auto prov = provenance(v);
      lim.ref = prov.ref;
      lim.verified = prov.verified;
      if (v["value"]) {
        if (v["min"] || v["max"]) fail(v, "use either value or min/max");
        lim.min = lim.max = number(v["value"]);
      }
      if (v["min"]) lim.min = number(v["min"]);
      if (v["max"]) lim.max = number(v["max"]);
      if (!lim.min && !lim.max) fail(v, section + "." + sym + " has no bounds");
      if (lim.min && lim.max && *lim.min > *lim.max) fail(v, section + "." + sym + ": min > max");
      out[sym] = std::move(lim);
    }
  }

  Expr expr(const YAML::Node& node, const std::string& where) const {
    if (!node.IsScalar()) fail(node, where + ": expected a number or expression");
    try {
      return Expr::parse(node.Scalar());
    } catch (const ExprError& e) {
      fail(node, where + ": bad expression '" + node.Scalar() + "': " + e.what() +
                     " at column " + std::to_string(e.column() + 1));
    }
  }

  // ac / dc: either flat min/max for all grades, or one block per grade.
  void load_params(const YAML::Node& node, const std::string& section,
                   std::map<std::string, Param, std::less<>>& out) {
    require_map(node, section);
    const bool is_dc = section == "dc";
    std::set<std::string> seen;
    for (const auto& kv : node) {
      const auto sym = kv.first.as<std::string>();
      const std::string where = section + "." + sym;
      if (!seen.insert(sym).second) fail(kv.first, "duplicate key '" + sym + "'");
      if (sym == "TCLCL") fail(kv.first, "TCLCL is defined by `grades`, not by `ac`");
      const auto& v = kv.second;
      require_map(v, where);
      if (is_dc) {
        check_keys(v, {"desc", "unit", "ref", "verified", "note", "min", "max", "cond", "8086",
                       "8086-2", "8086-1"});
        if (v["cond"]) {
          require_map(v["cond"], where + ".cond");
          for (const auto& c : v["cond"]) string(c.second);
        }
      } else {
        check_keys(v, {"desc", "unit", "ref", "verified", "note", "min", "max", "8086", "8086-2",
                       "8086-1"});
      }
      if (v["desc"]) string(v["desc"]);
      if (v["note"]) string(v["note"]);

      Param param;
      param.unit = unit(v);
      param.line = kv.first.Mark().line + 1;
      const auto prov = provenance(v);
      const bool flat = v["min"] || v["max"];
      const bool per_grade = v["8086"] || v["8086-2"] || v["8086-1"];
      if (flat && per_grade) fail(kv.first, where + ": use either min/max or per-grade blocks");
      if (!flat && !per_grade) fail(kv.first, where + " has no bounds");

      for (Grade g : kAllGrades) {
        const std::string gname(to_string(g));
        BoundSpec b{std::nullopt, std::nullopt, prov.ref, prov.verified};
        const YAML::Node src = per_grade ? v[gname] : v;
        if (per_grade) {
          if (!src) fail(kv.first, where + ": missing grade " + gname);
          require_map(src, where + "." + gname);
          check_keys(src, {"min", "max", "ref", "verified", "note"});
          if (src["ref"]) b.ref = string(src["ref"]);
          if (src["verified"]) b.verified = boolean(src["verified"]);
          if (src["note"]) string(src["note"]);
          if (!src["min"] && !src["max"]) fail(src, where + "." + gname + " has no bounds");
        }
        if (src["min"]) b.min = expr(src["min"], where);
        if (src["max"]) b.max = expr(src["max"], where);
        for (const auto* e : {b.min ? &*b.min : nullptr, b.max ? &*b.max : nullptr}) {
          if (e) check_vars(*e, is_dc, src, where);
        }
        param.grades[g] = std::move(b);
      }
      out[sym] = std::move(param);
    }
  }

  void check_vars(const Expr& e, bool is_dc, const YAML::Node& node,
                  const std::string& where) const {
    for (const Var& var : e.vars()) {
      if (is_dc) {
        if (var.name != "VCC" || var.bound) {
          fail(node, where + ": '" + var_text(var) + "' is not allowed; DC bounds may use VCC");
        }
      } else if (!var.bound && var.name != "TCLCL") {
        fail(node, where + ": '" + var.name + "' is not allowed; use TCLCL or SYM.min/SYM.max");
      }
    }
  }

  void load_decode(const YAML::Node& node) {
    require_map(node, "decode");
    check_keys(node, {"status", "segment", "queue", "byte_enable"});
    table(required(node, "status"), "status", impl_.decode.status);
    table(required(node, "segment"), "segment", impl_.decode.segment);
    table(required(node, "queue"), "queue", impl_.decode.queue);
    table(required(node, "byte_enable"), "byte_enable", impl_.decode.byte_enable);
  }

  template <std::size_t N>
  void table(const YAML::Node& node, const std::string& name, std::array<std::string, N>& out) {
    static_assert(N == 4 || N == 8);
    constexpr std::size_t kBits = N == 8 ? 3 : 2;
    const std::string where = "decode." + name;
    require_map(node, where);
    check_keys(node, {"bits", "ref", "verified", "table"});
    provenance(node);
    const YAML::Node bits = required(node, "bits");
    if (!bits.IsSequence() || bits.size() != kBits) {
      fail(bits, where + ".bits must list " + std::to_string(kBits) + " signals");
    }
    for (const auto& b : bits) string(b);
    const YAML::Node t = required(node, "table");
    require_map(t, where + ".table");
    std::array<bool, N> filled{};
    for (const auto& kv : t) {
      const auto key = kv.first.as<std::string>();
      if (key.size() != kBits || key.find_first_not_of("01") != std::string::npos) {
        fail(kv.first, where + ": key '" + key + "' must be " + std::to_string(kBits) +
                           " binary digits");
      }
      const auto idx = std::stoul(key, nullptr, 2);
      if (filled[idx]) fail(kv.first, where + ": duplicate key '" + key + "'");
      filled[idx] = true;
      out[idx] = string(kv.second);
    }
    for (std::size_t i = 0; i < N; ++i) {
      if (!filled[i]) fail(t, where + " is missing entries");
    }
  }

  // Evaluates every AC limit at both ends of each grade's clock range, so bad references,
  // cycles and min > max are load errors rather than test-time surprises.
  void check_ac() const {
    for (const auto& [sym, param] : impl_.ac) {
      for (Grade g : kAllGrades) {
        const GradeInfo& gi = impl_.grades.at(g);
        for (double t : {gi.tclcl_min_ns, gi.tclcl_max_ns}) {
          Limit lim;
          try {
            lim = impl_.eval_ac(sym, g, t);
          } catch (const SpecError& e) {
            throw SpecError(strip_location(e), file_, param.line);
          } catch (const ExprError& e) {
            throw SpecError("ac." + sym + ": " + e.what(), file_, param.line);
          }
          if (lim.min && lim.max && *lim.min > *lim.max) {
            throw SpecError("ac." + sym + ": min > max for grade " +
                                std::string(to_string(g)) + " at TCLCL = " + fmt(t) + " ns",
                            file_, param.line);
          }
        }
      }
    }
  }

  void check_dc() const {
    for (const auto& [sym, param] : impl_.dc) {
      for (Grade g : kAllGrades) {
        Limit lim;
        try {
          lim = impl_.eval_dc(sym, g);
        } catch (const ExprError& e) {
          throw SpecError("dc." + sym + ": " + e.what(), file_, param.line);
        }
        if (lim.min && lim.max && *lim.min > *lim.max) {
          throw SpecError("dc." + sym + ": min > max for grade " + std::string(to_string(g)),
                          file_, param.line);
        }
      }
    }
  }

  static std::string strip_location(const SpecError& e) {
    const std::string what = e.what();
    const std::string prefix = e.file() + ":" + std::to_string(e.line()) + ": ";
    return what.starts_with(prefix) ? what.substr(prefix.size()) : what;
  }

  static std::string fmt(double v) {
    std::string s = std::to_string(v);
    s.erase(s.find_last_not_of('0') + 1);
    if (s.back() == '.') s.pop_back();
    return s;
  }

  Spec::Impl& impl_;
  std::string file_;
};

}  // namespace

// ---------------------------------------------------------------------------------------------
// Public API

Spec::Spec(std::shared_ptr<const Impl> impl) : impl_(std::move(impl)) {}

Spec Spec::load(const std::filesystem::path& path) {
  auto impl = std::make_shared<Impl>();
  impl->path = path;
  YAML::Node root;
  try {
    root = YAML::LoadFile(path.string());
  } catch (const YAML::BadFile&) {
    throw SpecError("cannot open spec file", path.string(), 0);
  } catch (const YAML::Exception& e) {
    throw SpecError(e.msg, path.string(), e.mark.line + 1);
  }

  try {
    Loader(*impl).load(root);
  } catch (const YAML::Exception& e) {
    throw SpecError(e.msg, path.string(), e.mark.line + 1);
  }
  return Spec(std::move(impl));
}

int Spec::schema_version() const { return impl_->schema_version; }
const std::string& Spec::device_name() const { return impl_->device_name; }
const GradeInfo& Spec::grade(Grade g) const { return impl_->grades.at(g); }
const std::vector<Pin>& Spec::pins() const { return impl_->pins; }

const Pin& Spec::pin(std::string_view name) const {
  for (const auto& p : impl_->pins) {
    if (p.name == name) return p;
  }
  impl_->fail("unknown pin '" + std::string(name) + "'");
}

Limit Spec::ac(std::string_view sym, Grade g, double tclcl_ns) const {
  if (!std::isfinite(tclcl_ns) || tclcl_ns <= 0) {
    impl_->fail("Spec::ac(" + std::string(sym) + "): TCLCL must be a positive number of ns");
  }
  try {
    return impl_->eval_ac(sym, g, tclcl_ns);
  } catch (const ExprError& e) {
    impl_->fail("ac." + std::string(sym) + ": " + e.what());
  }
}

Limit Spec::dc(std::string_view sym, Grade g) const { return impl_->eval_dc(sym, g); }

Limit Spec::abs_max(std::string_view sym) const {
  const auto it = impl_->abs_max.find(sym);
  if (it == impl_->abs_max.end()) {
    impl_->fail("unknown abs_max parameter '" + std::string(sym) + "'");
  }
  return it->second;
}

const DecodeTables& Spec::decode() const { return impl_->decode; }

}  // namespace i8086::spec
