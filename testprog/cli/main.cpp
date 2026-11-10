// i86test — run the brief's test program across a grade x VCC x temperature matrix.

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "i8086/testprog/runner.hpp"

namespace {

using namespace i8086;

std::vector<std::string> split(const std::string& s) {
  std::vector<std::string> out;
  std::stringstream ss(s);
  for (std::string tok; std::getline(ss, tok, ',');) {
    if (!tok.empty()) out.push_back(tok);
  }
  return out;
}

std::vector<double> split_doubles(const std::string& s) {
  std::vector<double> out;
  for (const auto& tok : split(s)) out.push_back(std::stod(tok));
  return out;
}

void usage(std::ostream& os) {
  os << "usage: i86test [--spec FILE] [--die FILE] [--grade G[,G...]] [--vcc V[,V...]]\n"
        "               [--temp C[,C...]] [--tests SEL] [--out DIR] [--list]\n"
        "  SEL: comma-separated ids, prefixes or ranges, e.g. 'A*,D1-D11' (default '*')\n"
        "  G:   8086, 8086-2, 8086-1 (default: all)\n";
}

}  // namespace

int main(int argc, char** argv) {
  testprog::RunConfig cfg;
  bool list = false;
  try {
    for (int i = 1; i < argc; ++i) {
      const std::string arg = argv[i];
      auto value = [&]() -> std::string {
        if (i + 1 >= argc) throw std::invalid_argument("missing value for " + arg);
        return argv[++i];
      };
      if (arg == "--spec") cfg.spec_path = value();
      else if (arg == "--die") cfg.die_path = value();
      else if (arg == "--out") cfg.out_dir = value();
      else if (arg == "--tests") cfg.tests = value();
      else if (arg == "--vcc") cfg.vcc_v = split_doubles(value());
      else if (arg == "--temp") cfg.temp_c = split_doubles(value());
      else if (arg == "--grade") {
        cfg.grades.clear();
        for (const auto& g : split(value())) cfg.grades.push_back(spec::parse_grade(g));
      } else if (arg == "--list") list = true;
      else if (arg == "-h" || arg == "--help") {
        usage(std::cout);
        return 0;
      } else {
        throw std::invalid_argument("unknown argument " + arg);
      }
    }
  } catch (const std::exception& e) {
    std::cerr << "i86test: " << e.what() << "\n";
    usage(std::cerr);
    return 2;
  }

  try {
    if (list) {
      const auto& reg = testprog::TestRegistry::global();
      for (const auto& id : reg.select(cfg.tests)) {
        std::cout << id << "  " << reg.create(id)->title() << "\n";
      }
      return 0;
    }
    return testprog::run(cfg).ok() ? 0 : 1;
  } catch (const std::exception& e) {
    std::cerr << "i86test: " << e.what() << "\n";
    return 2;
  }
}
