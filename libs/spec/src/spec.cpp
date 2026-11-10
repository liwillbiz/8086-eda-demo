#include "i8086/spec/spec.hpp"

#include <yaml-cpp/yaml.h>

#include "i8086/common/errors.hpp"

namespace i8086::spec {

struct Spec::Impl {
  std::filesystem::path path;
  int schema_version = 0;
  std::string device_name;
  std::map<Grade, GradeInfo> grades;
  std::vector<Pin> pins;
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

namespace {

[[noreturn]] void fail(const std::filesystem::path& file, const YAML::Node& node,
                       const std::string& msg) {
  throw SpecError(msg, file.string(), node.Mark().line + 1);
}

Dir parse_dir(const std::filesystem::path& file, const YAML::Node& node) {
  const auto s = node.as<std::string>();
  if (s == "in") return Dir::In;
  if (s == "out") return Dir::Out;
  if (s == "io") return Dir::Io;
  if (s == "pwr") return Dir::Pwr;
  fail(file, node, "invalid pin dir '" + s + "'");
}

template <class T>
std::optional<T> opt(const YAML::Node& n) {
  if (!n) return std::nullopt;
  return n.as<T>();
}

}  // namespace

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
    if (!root["schema_version"]) fail(path, root, "missing schema_version");
    impl->schema_version = root["schema_version"].as<int>();
    if (impl->schema_version != 1) {
      fail(path, root["schema_version"], "unsupported schema_version");
    }
    impl->device_name = root["device"]["name"].as<std::string>();

    for (const auto& kv : root["grades"]) {
      const Grade g = parse_grade(kv.first.as<std::string>());
      const auto& v = kv.second;
      impl->grades[g] = GradeInfo{v["fclk_max_mhz"].as<double>(), v["tclcl_ns"][0].as<double>(),
                                  v["tclcl_ns"][1].as<double>()};
    }
    for (Grade g : kAllGrades) {
      if (!impl->grades.contains(g)) {
        fail(path, root["grades"], "missing grade " + std::string(to_string(g)));
      }
    }

    for (const auto& p : root["pins"]) {
      Pin pin;
      pin.num = p["num"].as<int>();
      pin.name = p["name"].as<std::string>();
      pin.dir = parse_dir(path, p["dir"]);
      pin.tristate = p["tristate"].as<bool>(false);
      pin.active_low = p["active_low"].as<bool>(false);
      pin.max_mode_name = opt<std::string>(p["max_mode_name"]);
      pin.bus = opt<std::string>(p["bus"]);
      pin.bit = opt<int>(p["bit"]);
      impl->pins.push_back(std::move(pin));
    }
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
  throw SpecError("unknown pin '" + std::string(name) + "'", impl_->path.string());
}

Limit Spec::ac(std::string_view sym, Grade, double) const {
  throw NotImplemented("Spec::ac(" + std::string(sym) + ") [M1]");
}

Limit Spec::dc(std::string_view sym, Grade) const {
  throw NotImplemented("Spec::dc(" + std::string(sym) + ") [M1]");
}

Limit Spec::abs_max(std::string_view sym) const {
  throw NotImplemented("Spec::abs_max(" + std::string(sym) + ") [M1]");
}

const DecodeTables& Spec::decode() const { throw NotImplemented("Spec::decode() [M1]"); }

}  // namespace i8086::spec
