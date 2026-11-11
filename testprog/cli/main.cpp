// i86test — run the brief's test program across a grade x VCC x temperature matrix.

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "i8086/testprog/runner.hpp"
#include "coverage.hpp"
#include "shmoo.hpp"

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
        "       i86test --shmoo [--spec FILE] [--die FILE] [--temp C] [--out DIR]\n"
        "       i86test --coverage EXPECTED [--spec FILE] [--dies DIR] [--grade ...] [--vcc ...]\n"
        "               [--temp ...] [--only DIE[,DIE...]] [--out DIR]\n"
        "  SEL: comma-separated ids, prefixes or ranges, e.g. 'A*,D1-D11' (default '*')\n"
        "  G:   8086, 8086-2, 8086-1 (default: all)\n"
        "  --shmoo writes shmoo.csv, shmoo.txt and shmoo.svg (VCC x MHz, design doc 6.5) and\n"
        "          exits 1 if a cell inside a grade's spec box fails\n"
        "  --coverage runs DIR/fault_*.yaml (default spec/dies) and every die EXPECTED lists, writes\n"
        "          coverage.md and coverage.csv, and exits 1 unless each die's failing tests are\n"
        "          exactly the ones EXPECTED lists for it; --only limits it to the named dies\n"
        "  I86_JOBS=N runs the matrix on N threads (default: one per core); output is the same\n";
}

}  // namespace

int main(int argc, char** argv) {
  testprog::RunConfig cfg;
  bool list = false, shmoo = false;
  std::filesystem::path coverage, dies_dir = "spec/dies";
  std::vector<std::string> only;
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
      else if (arg == "--shmoo") shmoo = true;
      else if (arg == "--coverage") coverage = value();
      else if (arg == "--dies") dies_dir = value();
      else if (arg == "--only") only = split(value());
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
    if (shmoo) {
      testprog::ShmooConfig sc;
      sc.spec_path = cfg.spec_path;
      sc.die_path = cfg.die_path;
      sc.temp_c = cfg.temp_c.empty() ? 25.0 : cfg.temp_c.front();
      const auto r = testprog::run_shmoo(sc);
      std::filesystem::create_directories(cfg.out_dir);
      std::ofstream csv(cfg.out_dir / "shmoo.csv");
      testprog::write_shmoo_csv(csv, r);
      std::ostringstream ascii;
      testprog::write_shmoo_ascii(ascii, r);
      std::ofstream(cfg.out_dir / "shmoo.txt") << ascii.str();
      std::ofstream svg(cfg.out_dir / "shmoo.svg");
      testprog::write_shmoo_svg(svg, r);
      std::cout << ascii.str();
      return r.box_fails() == 0 ? 0 : 1;
    }
    if (!coverage.empty()) {
      testprog::CoverageConfig cc{cfg, dies_dir, coverage, only};
      const auto r = testprog::run_coverage(cc, std::cout);
      std::filesystem::create_directories(cfg.out_dir);
      std::ofstream csv(cfg.out_dir / "coverage.csv");
      testprog::write_coverage_csv(csv, r);
      std::ofstream md(cfg.out_dir / "coverage.md");
      testprog::write_coverage_md(md, r);
      return r.ok() ? 0 : 1;
    }
    return testprog::run(cfg).ok() ? 0 : 1;
  } catch (const std::exception& e) {
    std::cerr << "i86test: " << e.what() << "\n";
    return 2;
  }
}
