#include <algorithm>
#include <format>
#include <map>
#include <set>

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

namespace {

std::string cell(Verdict v) {
  switch (v) {
    case Verdict::Pass: return "pass";
    case Verdict::Fail: return "**FAIL**";
    case Verdict::Error: return "**ERROR**";
    case Verdict::Skip: return "skip";
  }
  return "?";
}

std::string point_label(double vcc, double temp) { return std::format("{:g} V / {:g} °C", vcc, temp); }

std::string limit_text(const spec::Limit& l) {
  if (l.min && l.max && *l.min == *l.max) return std::format("= {:g}", *l.min);
  std::string s;
  if (l.min) s += std::format("≥ {:g}", *l.min);
  if (l.max) s += std::string(s.empty() ? "" : ", ") + std::format("≤ {:g}", *l.max);
  return s;
}

std::string md_escape(std::string s) {
  std::string out;
  for (char c : s) {
    if (c == '|') out += "\\|";
    else if (c == '\n') out += ' ';
    else out += c;
  }
  return out;
}

}  // namespace

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

  // Results matrix (brief §7 shared artifact 3): test x VCC x TA, one table per grade.
  std::vector<std::string> tests;
  for (const auto& r : s.rows) {
    if (std::ranges::find(tests, r.test) == tests.end()) tests.push_back(r.test);
  }
  os << "\n## Results matrix\n";
  for (spec::Grade g : cfg.grades) {
    os << "\n**" << spec::to_string(g) << "**\n\n| test |";
    for (double v : cfg.vcc_v) {
      for (double t : cfg.temp_c) os << ' ' << point_label(v, t) << " |";
    }
    os << "\n|---|";
    for (std::size_t i = 0; i < cfg.vcc_v.size() * cfg.temp_c.size(); ++i) os << "---|";
    os << '\n';
    for (const auto& id : tests) {
      os << "| " << id << " |";
      for (double v : cfg.vcc_v) {
        for (double t : cfg.temp_c) {
          const auto it = std::ranges::find_if(s.rows, [&](const ResultRow& r) {
            return r.test == id && r.point.grade == g && r.point.vcc_v == v && r.point.temp_c == t;
          });
          os << ' ' << (it == s.rows.end() ? "" : cell(it->verdict)) << " |";
        }
      }
      os << '\n';
    }
  }

  const bool any_bad = std::ranges::any_of(s.rows, [](const ResultRow& r) {
    return r.verdict == Verdict::Fail || r.verdict == Verdict::Error;
  });
  if (any_bad) {
    os << "\n## Failures and errors\n\n| test | grade | VCC | TA | verdict | message |\n|---|---|---|---|---|---|\n";
    for (const auto& r : s.rows) {
      if (r.verdict != Verdict::Fail && r.verdict != Verdict::Error) continue;
      os << "| " << r.test << " | " << spec::to_string(r.point.grade) << " | " << r.point.vcc_v << " | "
         << r.point.temp_c << " | " << to_string(r.verdict) << " | " << md_escape(r.message) << " |\n";
    }
  }

  // Worst margin per parametric limit (exact-match functional checks have no margin).
  struct Worst {
    const Record* rec = nullptr;
    int count = 0;
  };
  std::map<std::string, Worst> worst;
  for (const auto& rec : log.records()) {
    if (!rec.margin || (rec.limit.min && rec.limit.max && *rec.limit.min == *rec.limit.max)) continue;
    Worst& w = worst[rec.test + " " + rec.param];
    ++w.count;
    if (!w.rec || *rec.margin < *w.rec->margin) w.rec = &rec;
  }
  if (!worst.empty()) {
    os << "\n## Worst margin per parameter\n\n| test | param | worst value | limit | margin | at | results |\n"
          "|---|---|---|---|---|---|---|\n";
    for (const auto& [key, w] : worst) {
      const Record& r = *w.rec;
      os << "| " << r.test << " | " << r.param << " | " << std::format("{:g}", *r.value) << ' ' << r.limit.unit
         << " | " << limit_text(r.limit) << " | " << std::format("{:g}", *r.margin) << " | "
         << spec::to_string(r.point.grade) << ' ' << point_label(r.point.vcc_v, r.point.temp_c) << " | "
         << w.count << " |\n";
    }
  }

  // Unverified-limit banner (design doc §3.1): every judged result that rests on a
  // `verified: false` limit, grouped by test and parameter.
  std::map<std::string, std::set<std::string>> unverified;
  for (const auto& rec : log.records()) {
    if (rec.limit.verified || (!rec.limit.min && !rec.limit.max)) continue;
    unverified[rec.test + " `" + rec.param + "`"].insert(rec.limit.ref);
  }
  if (!unverified.empty()) {
    os << "\n> **Unverified limits:** these results rely on limits marked `verified: false` "
          "(brief values not yet checked against the datasheet, task X1):\n";
    for (const auto& [what, refs] : unverified) {
      os << ">\n> - " << what << " (";
      bool first = true;
      for (const auto& ref : refs) {
        os << (first ? "" : "; ") << ref;
        first = false;
      }
      os << ")\n";
    }
  }
}

}  // namespace i8086::testprog::report
