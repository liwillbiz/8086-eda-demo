#include "i8086/testprog/runner.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <format>
#include <fstream>

#include "../report/report.hpp"
#include "i8086/common/errors.hpp"
#include "i8086/model/device_model.hpp"
#include "i8086/tester/sim_tester.hpp"

namespace i8086::testprog {

int RunSummary::count(Verdict v) const {
  return static_cast<int>(std::ranges::count(rows, v, &ResultRow::verdict));
}

namespace {

std::string make_run_id() {
  const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm utc{};
#ifdef _WIN32
  gmtime_s(&utc, &now);
#else
  gmtime_r(&now, &utc);
#endif
  char buf[32];
  std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &utc);
  return buf;
}

std::string fmt_num(double v) { return std::format("{:g}", v); }

// "param = value, limit [min, max] unit: note" for the first failing record of a test.
std::string first_failure(const Datalog& log, std::size_t from) {
  for (std::size_t i = from; i < log.records().size(); ++i) {
    const Record& r = log.records()[i];
    if (r.result != Verdict::Fail) continue;
    std::string msg = r.param + " = " + (r.value ? fmt_num(*r.value) : "none");
    if (r.limit.min && r.limit.max && *r.limit.min == *r.limit.max) {
      msg += ", expected " + fmt_num(*r.limit.min);
    } else {
      msg += ", limit [" + (r.limit.min ? fmt_num(*r.limit.min) : "") + ", " +
             (r.limit.max ? fmt_num(*r.limit.max) : "") + "]";
    }
    if (!r.limit.unit.empty()) msg += " " + r.limit.unit;
    if (!r.note.empty()) msg += ": " + r.note;
    return msg;
  }
  return "failed without a failing datalog record";
}

}  // namespace

RunSummary run(const RunConfig& cfg, const TestRegistry& registry, std::ostream& console) {
  const spec::Spec spec = spec::Spec::load(cfg.spec_path);
  const model::DieProfile die = model::DieProfile::load(cfg.die_path);
  const golden::Emulator golden;
  const auto ids = registry.select(cfg.tests);

  Datalog log(make_run_id());
  RunSummary summary;

  for (spec::Grade grade : cfg.grades) {
    for (double vcc : cfg.vcc_v) {
      for (double temp : cfg.temp_c) {
        const Point point{die.name, grade, vcc, temp};
        for (const auto& id : ids) {
          ResultRow row{id, point, Verdict::Error, {}};
          try {
            auto test = registry.create(id);
            tester::SimTester t(spec, model::make_behavioral(spec, die, {}),
                                tester::ElectricalModel(die));
            tester::LevelSet levels;
            levels.vcc = vcc;
            t.set_levels(levels);
            t.set_temperature(temp);
            t.power(true);
            log.begin(id, point);
            const std::size_t first_record = log.records().size();
            TestContext ctx{t, spec, grade, vcc, temp, log, &golden};
            row.verdict = test->run(ctx);
            if (row.verdict == Verdict::Fail) row.message = first_failure(log, first_record);
          } catch (const NotImplemented& e) {
            row.message = e.what();
          } catch (const model::UnimplementedInstruction& e) {
            row.message = std::string("unimplemented instruction: ") + e.what();
          } catch (const std::exception& e) {
            row.message = std::string("exception: ") + e.what();
          }
          console << std::format("{:<4} {:<7} {:.2f}V {:>5.1f}C  {}{}\n", id,
                                 spec::to_string(grade), vcc, temp, to_string(row.verdict),
                                 row.message.empty() ? "" : "  (" + row.message + ")");
          summary.rows.push_back(std::move(row));
        }
      }
    }
  }

  std::filesystem::create_directories(cfg.out_dir);
  {
    std::ofstream f(cfg.out_dir / "datalog.jsonl");
    log.write_jsonl(f);
  }
  {
    std::ofstream f(cfg.out_dir / "results.csv");
    report::write_results_csv(f, summary);
  }
  {
    std::ofstream f(cfg.out_dir / "summary.md");
    report::write_summary_md(f, cfg, summary, log);
  }

  console << std::format("{} test(s) x {} point(s): {} pass, {} fail, {} error, {} skip\n",
                         ids.size(), cfg.grades.size() * cfg.vcc_v.size() * cfg.temp_c.size(),
                         summary.count(Verdict::Pass), summary.count(Verdict::Fail),
                         summary.count(Verdict::Error), summary.count(Verdict::Skip));
  return summary;
}

}  // namespace i8086::testprog
