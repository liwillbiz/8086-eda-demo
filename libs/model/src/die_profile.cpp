#include "i8086/model/die_profile.hpp"

#include <yaml-cpp/yaml.h>

#include "i8086/common/errors.hpp"
#include "i8086/spec/spec.hpp"

namespace i8086::model {

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
    if (root["faults"] && root["faults"].size() > 0) {
      throw NotImplemented("DieProfile fault parsing [M7]");
    }
  } catch (const YAML::BadFile&) {
    throw spec::SpecError("cannot open die profile", path.string(), 0);
  } catch (const YAML::Exception& e) {
    throw spec::SpecError(e.msg, path.string(), e.mark.line + 1);
  }
  return die;
}

}  // namespace i8086::model
