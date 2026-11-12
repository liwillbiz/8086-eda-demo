#include "i8086/testprog/runner.hpp"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <chrono>
#include <ctime>
#include <format>
#include <fstream>
#include <thread>

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

// Worker threads for the matrix: I86_JOBS if set (1 = serial), else one per core.
unsigned job_count(std::size_t points) {
  unsigned n = std::max(1u, std::thread::hardware_concurrency());
  if (const char* env = std::getenv("I86_JOBS")) {
    const int v = std::atoi(env);
    if (v > 0) n = static_cast<unsigned>(v);
  }
  return static_cast<unsigned>(std::min<std::size_t>(n, std::max<std::size_t>(points, 1)));
}

}  // namespace

RunSummary run(const RunConfig& cfg, const TestRegistry& registry, std::ostream& console) {
  const spec::Spec spec = spec::Spec::load(cfg.spec_path);
  const model::DieProfile die = model::DieProfile::load(cfg.die_path);
  const golden::Emulator golden;
  const auto ids = registry.select(cfg.tests);
  const std::string run_id = make_run_id();

  std::vector<Point> points;
  for (spec::Grade grade : cfg.grades) {
    for (double vcc : cfg.vcc_v) {
      for (double temp : cfg.temp_c) points.push_back({die.name, grade, vcc, temp});
    }
  }

  // Each point runs on its own DUT and datalog; results merge in matrix order, so the
  // output does not depend on the number of threads.
  struct PointResult {
    std::vector<ResultRow> rows;
    Datalog log{""};
    std::string console;
  };
  std::vector<PointResult> results(points.size());
  const auto run_point = [&](std::size_t i) {
    const Point& point = points[i];
    PointResult& out = results[i];
    for (const auto& id : ids) {
      ResultRow row{id, point, Verdict::Error, {}};
      try {
        auto test = registry.create(id);
        tester::SimTester t(spec, model::make_behavioral(spec, die, {}), tester::ElectricalModel(die));
        tester::LevelSet levels;
        levels.vcc = point.vcc_v;
        t.set_levels(levels);
        t.set_temperature(point.temp_c);
        t.power(true);
        out.log.begin(id, point);
        const std::size_t first_record = out.log.records().size();
        TestContext ctx{t, spec, point.grade, point.vcc_v, point.temp_c, out.log, &golden};
        row.verdict = test->run(ctx);
        if (row.verdict == Verdict::Fail) row.message = first_failure(out.log, first_record);
      } catch (const NotImplemented& e) {
        row.message = e.what();
      } catch (const model::UnimplementedInstruction& e) {
        row.message = std::string("unimplemented instruction: ") + e.what();
      } catch (const std::exception& e) {
        row.message = std::string("exception: ") + e.what();
      }
      out.console += std::format("{:<4} {:<7} {:.2f}V {:>5.1f}C  {}{}\n", id, spec::to_string(point.grade),
                                 point.vcc_v, point.temp_c, to_string(row.verdict),
                                 row.message.empty() ? "" : "  (" + row.message + ")");
      out.rows.push_back(std::move(row));
    }
  };

  std::atomic<std::size_t> next{0};
  std::vector<std::jthread> workers;
  for (unsigned w = job_count(points.size()); w > 0; --w) {
    workers.emplace_back([&] {
      for (std::size_t i; (i = next++) < points.size();) run_point(i);
    });
  }
  workers.clear();

  Datalog log(run_id);
  RunSummary summary;
  for (PointResult& r : results) {
    console << r.console;
    for (const Record& rec : r.log.records()) {
      log.begin(rec.test, rec.point);
      log.record(rec.param, rec.value, rec.limit, rec.result, rec.note);
    }
    for (ResultRow& row : r.rows) summary.rows.push_back(std::move(row));
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
