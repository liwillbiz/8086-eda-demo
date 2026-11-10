#include "i8086/testprog/datalog.hpp"

#include <algorithm>
#include <cstdio>

namespace i8086::testprog {

std::string_view to_string(Verdict v) {
  switch (v) {
    case Verdict::Pass: return "pass";
    case Verdict::Fail: return "fail";
    case Verdict::Error: return "error";
    case Verdict::Skip: return "skip";
  }
  return "?";
}

Datalog::Datalog(std::string run_id) : run_id_(std::move(run_id)) {}

void Datalog::begin(std::string test, const Point& point) {
  test_ = std::move(test);
  point_ = point;
}

void Datalog::record(std::string param, std::optional<double> value, const spec::Limit& limit,
                     Verdict result, std::string note) {
  std::optional<double> margin;
  if (value) {
    if (limit.min) margin = *value - *limit.min;
    if (limit.max) margin = std::min(margin.value_or(*limit.max - *value), *limit.max - *value);
  }
  records_.push_back(Record{run_id_, test_, point_, std::move(param), value, limit, result, margin,
                            std::move(note)});
}

namespace {

std::string json_string(std::string_view s) {
  std::string out = "\"";
  for (char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof buf, "\\u%04x", c);
          out += buf;
        } else {
          out += c;
        }
    }
  }
  return out + "\"";
}

std::string json_number(std::optional<double> v) {
  if (!v) return "null";
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.6g", *v);
  return buf;
}

}  // namespace

void Datalog::write_jsonl(std::ostream& os) const {
  for (const auto& r : records_) {
    os << "{\"run\":" << json_string(r.run) << ",\"test\":" << json_string(r.test)
       << ",\"grade\":" << json_string(spec::to_string(r.point.grade))
       << ",\"vcc\":" << json_number(r.point.vcc_v) << ",\"temp\":" << json_number(r.point.temp_c)
       << ",\"die\":" << json_string(r.point.die) << ",\"param\":" << json_string(r.param)
       << ",\"value\":" << json_number(r.value) << ",\"min\":" << json_number(r.limit.min)
       << ",\"max\":" << json_number(r.limit.max) << ",\"unit\":" << json_string(r.limit.unit)
       << ",\"result\":" << json_string(to_string(r.result))
       << ",\"margin\":" << json_number(r.margin) << ",\"limit_ref\":" << json_string(r.limit.ref)
       << ",\"limit_verified\":" << (r.limit.verified ? "true" : "false")
       << ",\"note\":" << json_string(r.note) << "}\n";
  }
}

}  // namespace i8086::testprog
