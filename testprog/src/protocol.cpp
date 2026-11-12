#include "protocol.hpp"

#include <format>
#include <map>

#include "functional.hpp"

namespace i8086::testprog {
namespace {

using model::BusCycleType;
using model::Logic;
using model::TState;

std::string_view name_of(BusCycleType t) {
  switch (t) {
    case BusCycleType::None: return "None";
    case BusCycleType::CodeFetch: return "CodeFetch";
    case BusCycleType::MemRead: return "MemRead";
    case BusCycleType::MemWrite: return "MemWrite";
    case BusCycleType::IoRead: return "IoRead";
    case BusCycleType::IoWrite: return "IoWrite";
    case BusCycleType::InterruptAck: return "InterruptAck";
    case BusCycleType::Halt: return "Halt";
  }
  return "?";
}

struct Edge {
  uint64_t cycle;
  int64_t t;
  Logic v;
};

// Resolved pin waveforms rebuilt from the per-period trace events.
class Waves {
 public:
  explicit Waves(const tester::BusTrace& trace) {
    for (const auto& c : trace.cycles) {
      for (const auto& e : c.events) edges_[e.pin].push_back({c.clk, e.delay_ps, e.value});
    }
  }
  const std::vector<Edge>& edges(int pin) const {
    static const std::vector<Edge> none;
    auto it = edges_.find(pin);
    return it == edges_.end() ? none : it->second;
  }
  Logic at(int pin, int64_t t) const {
    Logic v = Logic::Z;
    for (const auto& e : edges(pin)) {
      if (e.t > t) break;
      v = e.v;
    }
    return v;
  }
  // Edges of `pin` to `v` inside periods [first, last].
  std::vector<Edge> to(int pin, Logic v, uint64_t first, uint64_t last) const {
    std::vector<Edge> out;
    for (const auto& e : edges(pin)) {
      if (e.cycle >= first && e.cycle <= last && e.v == v) out.push_back(e);
    }
    return out;
  }

 private:
  std::map<int, std::vector<Edge>> edges_;
};

int bit(Logic v) { return v == Logic::L0 ? 0 : v == Logic::L1 ? 1 : -1; }

struct Pins {
  int ale, rd, wr, inta, den, dtr, mio, bhe, a16, a17, a18, a19;
  explicit Pins(const spec::Spec& s)
      : ale(s.pin("ALE").num), rd(s.pin("RD_N").num), wr(s.pin("WR_N").num),
        inta(s.pin("INTA_N").num), den(s.pin("DEN_N").num), dtr(s.pin("DT_R_N").num),
        mio(s.pin("M_IO_N").num), bhe(s.pin("BHE_N").num), a16(s.pin("A16").num),
        a17(s.pin("A17").num), a18(s.pin("A18").num), a19(s.pin("A19").num) {}
};

bool is_read(BusCycleType t) {
  return t == BusCycleType::CodeFetch || t == BusCycleType::MemRead || t == BusCycleType::IoRead;
}
bool is_write(BusCycleType t) { return t == BusCycleType::MemWrite || t == BusCycleType::IoWrite; }
bool is_memory(BusCycleType t) {
  return t == BusCycleType::CodeFetch || t == BusCycleType::MemRead || t == BusCycleType::MemWrite;
}

// Time inside the cycle's strobe at which status pins are read: just before the strobe
// ends, when S3-S6 have been valid since T2.
std::optional<int64_t> strobe_end(const Waves& w, const Pins& p, const tester::BusTransaction& tx) {
  const int strobe = is_write(tx.type) ? p.wr : tx.type == BusCycleType::InterruptAck ? p.inta : p.rd;
  const uint64_t t4 = tx.start_clk + 3 + static_cast<uint64_t>(tx.wait_states);
  const auto rises = w.to(strobe, Logic::L1, tx.start_clk + 1, t4);
  if (rises.empty()) return std::nullopt;
  return rises.front().t - 1;
}

CycleStatus status_at(const Waves& w, const Pins& p, const spec::Spec& spec,
                      const tester::BusTransaction& tx, int64_t t) {
  CycleStatus s;
  s.s3 = bit(w.at(p.a16, t));
  s.s4 = bit(w.at(p.a17, t));
  s.s5 = bit(w.at(p.a18, t));
  s.s6 = bit(w.at(p.a19, t));
  const auto& d = spec.decode();
  if (s.s3 >= 0 && s.s4 >= 0) s.segment = d.segment[static_cast<std::size_t>(s.s4 << 1 | s.s3)];
  s.transfer = d.byte_enable[static_cast<std::size_t>((tx.bhe_n ? 2 : 0) | (tx.address & 1))];
  return s;
}

}  // namespace

CycleStatus cycle_status(const tester::BusTrace& trace, const spec::Spec& spec,
                         const tester::BusTransaction& tx) {
  const Waves w(trace);
  const Pins p(spec);
  const auto t = strobe_end(w, p, tx);
  if (!t) {
    CycleStatus s;
    s.transfer = spec.decode().byte_enable[static_cast<std::size_t>((tx.bhe_n ? 2 : 0) | (tx.address & 1))];
    return s;
  }
  return status_at(w, p, spec, tx, *t);
}

std::vector<std::string> check_protocol(const tester::BusTrace& trace, const spec::Spec& spec,
                                        std::size_t max_messages) {
  const Waves w(trace);
  const Pins p(spec);
  std::vector<std::string> out;
  std::size_t extra = 0;

  for (std::size_t i = 0; i < trace.transactions.size(); ++i) {
    const auto& tx = trace.transactions[i];
    const std::string where = std::format("cycle {} ({} {} at CLK {})", i, name_of(tx.type),
                                          hex(tx.address, 5), tx.start_clk);
    auto fail = [&](std::string what) {
      if (out.size() < max_messages) out.push_back(where + ": " + what);
      else ++extra;
    };
    const uint64_t t1 = tx.start_clk;

    if (tx.type == BusCycleType::None) {
      fail("unclassified bus cycle");
      continue;
    }
    if (w.to(p.ale, Logic::L1, t1, t1).size() != 1 || w.to(p.ale, Logic::L0, t1, t1).size() != 1) {
      fail("no single ALE pulse in T1");
    }
    if (tx.type == BusCycleType::Halt) continue;

    const uint64_t t4 = t1 + 3 + static_cast<uint64_t>(tx.wait_states);
    if (t4 < trace.cycles.size()) {
      std::vector<TState> want{TState::T1, TState::T2, TState::T3};
      want.insert(want.end(), static_cast<std::size_t>(tx.wait_states), TState::Tw);
      want.push_back(TState::T4);
      for (std::size_t k = 0; k < want.size(); ++k) {
        if (trace.cycles[t1 + k].bus.t != want[k]) {
          fail(std::format("T-state order broken at CLK {}", t1 + k));
          break;
        }
      }
    }

    const int strobe = is_write(tx.type) ? p.wr : tx.type == BusCycleType::InterruptAck ? p.inta : p.rd;
    const auto falls = w.to(strobe, Logic::L0, t1, t4);
    const auto rises = w.to(strobe, Logic::L1, t1, t4);
    if (falls.size() != 1 || falls.front().cycle != t1 + 1) fail("strobe did not go active in T2");
    if (rises.size() != 1 || rises.front().cycle != t4) fail("strobe did not go inactive in T4");
    for (int other : {p.rd, p.wr, p.inta}) {
      if (other != strobe && !w.to(other, Logic::L0, t1, t4).empty()) fail("a second strobe went active");
    }
    const auto den_on = w.to(p.den, Logic::L0, t1, t1 + 1);
    const auto den_off = w.to(p.den, Logic::L1, t1 + 2, t4);
    if (den_on.size() != 1 || den_off.size() != 1 || den_off.front().cycle != t4) {
      fail("DEN# not active from T1/T2 to T4");
    }
    if (rises.size() != 1) continue;

    const int64_t t = rises.front().t - 1;
    const Logic dtr = w.at(p.dtr, t), mio = w.at(p.mio, t);
    if (is_read(tx.type) && dtr != Logic::L0) fail("DT/R# not low during a read");
    if (is_write(tx.type) && dtr != Logic::L1) fail("DT/R# not high during a write");
    if (tx.type != BusCycleType::InterruptAck && (mio == Logic::L1) != is_memory(tx.type)) {
      fail("M/IO# does not match the cycle type");
    }
    const CycleStatus s = status_at(w, p, spec, tx, t);
    if (s.s6 != 0) fail("S6 is not 0");
    if (tx.type == BusCycleType::CodeFetch && s.segment != "CS_or_none") {
      fail("code fetch reports segment " + (s.segment.empty() ? std::string("X") : s.segment));
    }
    if (s.transfer == "none") fail("BHE#/A0 = 1/1 (no byte lane) on a data transfer");
  }
  if (extra) out.push_back(std::format("... and {} more violation(s)", extra));
  return out;
}

}  // namespace i8086::testprog
