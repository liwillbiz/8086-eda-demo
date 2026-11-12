#include <algorithm>

#include "report.hpp"

namespace i8086::testprog::report {

void write_results_csv(std::ostream& os, const RunSummary& s) {
  os << "test,die,grade,vcc,temp,verdict,message\n";
  for (const auto& r : s.rows) {
    std::string msg = r.message;
    std::ranges::replace(msg, '"', '\'');
    os << r.test << ',' << r.point.die << ',' << spec::to_string(r.point.grade) << ','
       << r.point.vcc_v << ',' << r.point.temp_c << ',' << to_string(r.verdict) << ",\"" << msg
       << "\"\n";
  }
}

void write_summary_md(std::ostream& os, const RunConfig& cfg, const RunSummary& s,
                      const Datalog& log) {
  os << "# i86test summary\n\n"
     << "- run: `" << log.run_id() << "`\n"
     << "- spec: `" << cfg.spec_path.string() << "`\n"
     << "- die: `" << cfg.die_path.string() << "`\n"
     << "- tests: `" << cfg.tests << "`\n\n"
     << "| pass | fail | error | skip |\n|---|---|---|---|\n"
     << "| " << s.count(Verdict::Pass) << " | " << s.count(Verdict::Fail) << " | "
     << s.count(Verdict::Error) << " | " << s.count(Verdict::Skip) << " |\n";

  if (s.rows.empty()) {
    os << "\nNo test cases are registered yet.\n";
    return;
  }
  os << "\n| test | grade | VCC | TA | verdict | message |\n|---|---|---|---|---|---|\n";
  for (const auto& r : s.rows) {
    os << "| " << r.test << " | " << spec::to_string(r.point.grade) << " | " << r.point.vcc_v
       << " | " << r.point.temp_c << " | " << to_string(r.verdict) << " | " << r.message << " |\n";
  }

  const bool unverified = std::ranges::any_of(log.records(), [](const Record& rec) {
    return !rec.limit.verified && (rec.limit.min || rec.limit.max);
  });
  if (unverified) {
    os << "\n> **Unverified limits:** some results rely on spec values with `verified: false`.\n";
  }
}

}  // namespace i8086::testprog::report
