#include "rom_case.hpp"

#include <format>

namespace i8086::testprog {

Verdict worst(Verdict a, Verdict b) {
  auto rank = [](Verdict v) {
    switch (v) {
      case Verdict::Error: return 3;
      case Verdict::Fail: return 2;
      case Verdict::Skip: return 1;
      case Verdict::Pass: return 0;
    }
    return 3;
  };
  return rank(a) >= rank(b) ? a : b;
}

std::optional<uint16_t> nth(const std::vector<uint16_t>& v, std::size_t i) {
  if (i < v.size()) return v[i];
  return std::nullopt;
}

Verdict expect_word(TestContext& ctx, std::string param, std::optional<uint16_t> actual,
                    uint16_t expected, std::string ref, uint16_t care) {
  const uint16_t want = expected & care;
  const spec::Limit lim = exact(want, std::move(ref));
  const std::string mask = care == 0xFFFF ? "" : std::format(" under mask {}", hex(care));
  if (!actual) {
    ctx.log.record(std::move(param), std::nullopt, lim, Verdict::Fail,
                   std::format("expected {}{}, not reported", hex(want), mask));
    return Verdict::Fail;
  }
  const uint16_t got = *actual & care;
  return check(ctx, std::move(param), got, lim,
               std::format("expected {}, got {}{}", hex(want), hex(got), mask));
}

Verdict RomCase::run(TestContext& ctx) {
  setup_functional(ctx);
  const RomRun r = run_rom(ctx, rom_name());

  Verdict v = check(ctx, "halted", r.actual.halted ? 1 : 0, exact(1, ref()),
                    r.actual.halted ? "" : std::format("no HLT within {} CLK", r.result.cycles));
  v = worst(v, check(ctx, "protocol_violations", static_cast<double>(r.protocol.size()),
                     exact(0, "brief §4.1, spec decode"),
                     r.protocol.empty() ? "" : r.protocol.front()));
  const auto d = diff(r.expected, r.actual, diff_options());
  std::string note;
  for (const auto& m : d) note += (note.empty() ? "" : "; ") + m;
  v = worst(v, check(ctx, "golden_mismatches", static_cast<double>(d.size()),
                     exact(0, "golden emulator (libs/golden)"), note));
  return worst(v, brief_checks(ctx, r));
}

}  // namespace i8086::testprog
