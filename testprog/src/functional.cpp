#include "functional.hpp"

#include <algorithm>
#include <format>
#include <stdexcept>

#include "embedded.hpp"
#include "i8086/common/errors.hpp"
#include "i8086/golden/rom.hpp"
#include "protocol.hpp"

namespace i8086::testprog {

std::span<const uint8_t> rom(std::string_view name) {
  if (!embedded::nasm_found()) {
    throw NotImplemented("test ROM " + std::string(name) +
                         " was not built: NASM was not found at configure time (decision D7)");
  }
  for (const auto& b : embedded::roms()) {
    if (b.name == name) return b.bytes;
  }
  throw std::invalid_argument("unknown test ROM " + std::string(name));
}

tester::Pattern pattern(std::string_view name) {
  for (const auto& b : embedded::patterns()) {
    if (b.name == name) {
      const std::string_view text(reinterpret_cast<const char*>(b.bytes.data()), b.bytes.size());
      return tester::Pattern::parse(text, std::string(name) + ".pat");
    }
  }
  throw std::invalid_argument("unknown pattern " + std::string(name));
}

tester::TimingSet functional_timing(const spec::Spec& spec, spec::Grade grade) {
  tester::TimingSet ts;
  ts.period_ns = spec.grade(grade).tclcl_min_ns;
  return ts;
}

void setup_functional(TestContext& ctx) { ctx.t.set_timing(0, functional_timing(ctx.spec, ctx.grade)); }

std::vector<uint16_t> Observation::words(uint16_t port) const {
  std::vector<uint16_t> out;
  for (std::size_t i = 0; i < io.size(); ++i) {
    if (io[i].port != port) continue;
    uint16_t w = io[i].value;
    if (i + 1 < io.size() && io[i + 1].port == port + 1) {
      w = static_cast<uint16_t>(w | io[i + 1].value << 8);
      ++i;
    }
    out.push_back(w);
  }
  return out;
}

Observation observe(const golden::Result& r) {
  Observation o;
  for (const auto& w : r.io_writes) {
    o.io.push_back({w.port, static_cast<uint8_t>(w.value & 0xFF)});
    if (w.word) o.io.push_back({static_cast<uint16_t>(w.port + 1), static_cast<uint8_t>(w.value >> 8)});
  }
  for (const auto& w : r.mem_writes) o.mem.push_back({w.addr, w.value});
  o.halted = r.stop == golden::StopReason::Halt;
  return o;
}

Observation observe(const tester::BusTrace& trace, bool halted) {
  using T = model::BusCycleType;
  Observation o;
  for (const auto& tx : trace.transactions) {
    if (tx.type != T::MemWrite && tx.type != T::IoWrite) continue;
    const bool odd = (tx.address & 1) != 0;
    const uint32_t mask = tx.type == T::MemWrite ? 0xFFFFFu : 0xFFFFu;
    const uint32_t base = tx.address & ~1u & mask;
    auto put = [&](uint32_t addr, uint8_t v) {
      if (tx.type == T::MemWrite) o.mem.push_back({addr & mask, v});
      else o.io.push_back({static_cast<uint16_t>(addr & mask), v});
    };
    if (!odd) put(base, static_cast<uint8_t>(tx.data & 0xFF));
    if (!tx.bhe_n) put(base + 1, static_cast<uint8_t>(tx.data >> 8));
  }
  o.halted = halted;
  return o;
}

std::string hex(uint32_t v, int digits) { return std::format("{:0{}X}h", v, digits); }

std::vector<std::string> diff(const Observation& g, const Observation& d, const DiffOptions& opt) {
  std::vector<std::string> out;
  std::size_t extra = 0;
  auto add = [&](std::string s) {
    if (out.size() < opt.max_messages) out.push_back(std::move(s));
    else ++extra;
  };

  if (g.halted != d.halted) {
    add(std::format("halt: golden {}, DUT {}", g.halted ? "halted" : "did not halt",
                    d.halted ? "halted" : "did not halt"));
  }

  // I/O: exact, except FLAGS reports under the undefined-flag mask.
  std::size_t flag_reports = 0;
  const std::size_t n_io = std::min(g.io.size(), d.io.size());
  for (std::size_t i = 0; i < n_io; ++i) {
    const IoByte& a = g.io[i];
    const IoByte& b = d.io[i];
    uint8_t mask = 0;
    if (a.port == kPortFlags || a.port == kPortFlags + 1) {
      const std::size_t k = a.port == kPortFlags ? flag_reports++ : (flag_reports ? flag_reports - 1 : 0);
      const uint16_t undef = k < opt.per_report.size() ? opt.per_report[k] : opt.undefined_flags;
      mask = static_cast<uint8_t>(a.port == kPortFlags ? undef & 0xFF : undef >> 8);
    }
    if (a.port != b.port || (a.value & ~mask) != (b.value & ~mask)) {
      add(std::format("io[{}]: golden OUT {} <- {}, DUT OUT {} <- {}{}", i, hex(a.port), hex(a.value, 2),
                      hex(b.port), hex(b.value, 2),
                      mask ? std::format(" (undefined-flag mask {})", hex(mask, 2)) : ""));
    }
  }
  if (g.io.size() != d.io.size()) {
    add(std::format("io: golden wrote {} byte(s), DUT {}", g.io.size(), d.io.size()));
  }

  // Memory: exact and in order outside the stack window; stack bytes by count.
  auto split = [&](const std::vector<MemByte>& v, std::vector<MemByte>& rest) {
    std::size_t stack = 0;
    for (const auto& m : v) {
      if (m.addr >= opt.stack_lo && m.addr < opt.stack_hi) ++stack;
      else rest.push_back(m);
    }
    return stack;
  };
  std::vector<MemByte> gm, dm;
  const std::size_t gs = split(g.mem, gm), ds = split(d.mem, dm);
  const std::size_t n_mem = std::min(gm.size(), dm.size());
  for (std::size_t i = 0; i < n_mem; ++i) {
    if (gm[i] != dm[i]) {
      add(std::format("mem[{}]: golden [{}] <- {}, DUT [{}] <- {}", i, hex(gm[i].addr, 5),
                      hex(gm[i].value, 2), hex(dm[i].addr, 5), hex(dm[i].value, 2)));
    }
  }
  if (gm.size() != dm.size()) {
    add(std::format("mem: golden wrote {} byte(s) outside the stack, DUT {}", gm.size(), dm.size()));
  }
  if (gs != ds) add(std::format("stack: golden wrote {} byte(s), DUT {}", gs, ds));
  if (extra) out.push_back(std::format("... and {} more difference(s)", extra));
  return out;
}

RomRun run_rom(TestContext& ctx, std::string_view rom_name, const tester::RunOpts& opts,
               std::span<const golden::InterruptRequest> interrupts) {
  const auto bytes = rom(rom_name);
  if (!ctx.golden) throw std::logic_error("run_rom: no golden emulator in the test context");

  RomRun run;
  golden::Program program = golden::rom_program({bytes.begin(), bytes.end()});
  program.interrupts.assign(interrupts.begin(), interrupts.end());
  run.golden = ctx.golden->run(program);
  if (run.golden.stop != golden::StopReason::Halt) {
    throw std::runtime_error(std::format("golden emulator did not halt on ROM {} (stop reason {})",
                                         rom_name, static_cast<int>(run.golden.stop)));
  }
  run.expected = observe(run.golden);

  tester::MemoryImage image;
  image.load(kRomTop - static_cast<uint32_t>(bytes.size()), bytes);
  run.result = ctx.t.run_program(image, opts);
  run.actual = observe(ctx.t.last_trace(), run.result.pass);
  run.protocol = check_protocol(ctx.t.last_trace(), ctx.spec);
  return run;
}

spec::Limit exact(double value, std::string ref, std::string unit) {
  spec::Limit l;
  l.min = value;
  l.max = value;
  l.unit = std::move(unit);
  l.ref = std::move(ref);
  l.verified = true;
  return l;
}

Verdict check(TestContext& ctx, std::string param, double value, const spec::Limit& lim,
              std::string note) {
  const bool ok = (!lim.min || value >= *lim.min) && (!lim.max || value <= *lim.max);
  const Verdict v = ok ? Verdict::Pass : Verdict::Fail;
  ctx.log.record(std::move(param), value, lim, v, std::move(note));
  return v;
}

}  // namespace i8086::testprog
