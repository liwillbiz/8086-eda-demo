#include "i8086/model/die_profile.hpp"

#include <yaml-cpp/yaml.h>

#include "i8086/common/errors.hpp"
#include "i8086/spec/spec.hpp"

namespace i8086::model {
namespace {

// One `faults:` entry: `{ type: StuckAt, pin: AD3, value: 1 }` etc. (design doc §4.2).
Fault parse_fault(const YAML::Node& n, const std::string& file) {
  const auto bad = [&](const std::string& what) {
    return spec::SpecError("fault: " + what, file, n.Mark().line + 1);
  };
  if (!n.IsMap() || !n["type"]) throw bad("expected a map with a `type`");
  const std::string type = n["type"].as<std::string>();
  const auto req = [&](const char* key) {
    if (!n[key]) throw bad(type + " needs `" + key + "`");
    return n[key];
  };
  if (type == "StuckAt") {
    StuckAt f{req("pin").as<std::string>(), req("value").as<int>(), std::nullopt};
    if (f.value != 0 && f.value != 1) throw bad("StuckAt value must be 0 or 1");
    if (n["start_clk"]) f.start_clk = n["start_clk"].as<uint64_t>();
    return f;
  }
  if (type == "SlowTiming") {
    SlowTiming f;
    // `params: all` (or no `params`) leaves the list empty, which means every delay.
    const YAML::Node p = n["params"];
    if (p && !(p.IsScalar() && p.as<std::string>() == "all")) {
      for (const auto& s : p) f.params.push_back(s.as<std::string>());
    }
    f.scale = n["scale"] ? n["scale"].as<double>() : 1.0;
    f.add_ns = n["add_ns"] ? n["add_ns"].as<double>() : 0.0;
    if (!(f.scale > 0)) throw bad("SlowTiming scale must be > 0");
    return f;
  }
  if (type == "SetupDegrade") {
    const std::string input = req("input").as<std::string>();
    SetupDegrade f;
    if (input == "data") f.input = InputClass::Data;
    else if (input == "ready") f.input = InputClass::Ready;
    else if (input == "intr") f.input = InputClass::Intr;
    else throw bad("SetupDegrade input must be data, ready or intr");
    f.add_ns = req("add_ns").as<double>();
    return f;
  }
  if (type == "LowVccFail") return LowVccFail{req("vcc_floor_v").as<double>()};
  if (type == "Leaky") return Leaky{req("pin").as<std::string>(), req("leakage_ua").as<double>()};
  if (type == "LogicBug") return LogicBug{req("name").as<std::string>()};
  throw bad("unknown type " + type);
}

}  // namespace

DieProfile DieProfile::load(const std::filesystem::path& path) {
  DieProfile die;
  try {
    const YAML::Node root = YAML::LoadFile(path.string());
    die.name = root["name"].as<std::string>();
    for (const auto& kv : root["timing_ns"]) {
      die.timing_ns[kv.first.as<std::string>()] = kv.second.as<double>();
    }
    for (const auto& kv : root["electrical"]) {
      die.electrical[kv.first.as<std::string>()] = kv.second.as<double>();
    }
    for (const auto& f : root["faults"]) die.faults.faults.push_back(parse_fault(f, path.string()));
  } catch (const YAML::BadFile&) {
    throw spec::SpecError("cannot open die profile", path.string(), 0);
  } catch (const YAML::Exception& e) {
    throw spec::SpecError(e.msg, path.string(), e.mark.line + 1);
  }
  return die;
}

}  // namespace i8086::model
