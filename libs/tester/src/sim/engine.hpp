#pragma once

// Layer-private picosecond waveform engine behind SimTester. It owns the tester-driven
// and DUT-driven waveforms of every pin, clocks the DeviceModel one period at a time,
// and decodes bus cycles from the pins the way an external logic analyzer would.

#include <cstdint>
#include <optional>
#include <vector>

#include "i8086/model/device_model.hpp"
#include "i8086/spec/spec.hpp"
#include "i8086/tester/electrical_model.hpp"
#include "i8086/tester/tester.hpp"
#include "timing.hpp"

namespace i8086::tester::detail {

struct Change {
  int64_t t;
  model::Logic v;
};

class Timeline {
 public:
  explicit Timeline(model::Logic initial = model::Logic::Z) : initial_(initial) {}
  void set(int64_t t, model::Logic v);         // a later set() at the same time wins
  model::Logic at(int64_t t) const;            // value after every change at or before t
  const Change* last_before(int64_t t) const;  // last change strictly before t
  model::Logic initial() const { return initial_; }
  const std::vector<Change>& changes() const { return changes_; }

 private:
  model::Logic initial_;
  std::vector<Change> changes_;
};

model::Logic resolve(model::Logic tester, model::Logic dut);

struct BusPins {
  int ale, rd_n, wr_n, inta_n, m_io_n, dt_r_n, bhe_n;
  std::vector<int> ad;  // AD0..AD15
  std::vector<int> a;   // A16..A19
  explicit BusPins(const spec::Spec&);
};

class Engine {
 public:
  Engine(const spec::Spec&, model::DeviceModel&, const ElectricalModel&, const LevelSet&,
         double temp_c);

  const spec::Spec& spec() const { return spec_; }
  const BusPins& bus() const { return bus_; }
  int pin_num(std::string_view name) const { return spec_.pin(name).num; }
  const spec::Pin& pin(int num) const { return *by_num_.at(static_cast<std::size_t>(num)); }
  std::string_view pin_name(int num) const;

  void begin_period(const ResolvedTiming&);
  const ResolvedTiming& timing() const { return *timing_; }
  uint64_t index() const { return index_; }
  int64_t start() const { return start_; }
  int64_t period_start(uint64_t k) const { return periods_.at(k).start; }
  const model::Conditions& conditions() const { return cond_; }

  // Tester drive: '0' -> VIL, '1' -> VIH (through the die's input thresholds), 'Z' = off.
  void drive(int pin, int64_t t, char c);
  void clock();  // CLK falling edge at start(), rising edge at start() + low time
  void end_period();

  const model::BusState& bus_state() const { return periods_.back().bus; }
  const model::BusState& bus_state_at(uint64_t k) const { return periods_.at(k).bus; }
  model::Logic dut_at(int pin, int64_t t) const { return dut_[idx(pin)].at(t); }
  model::Logic resolved_at(int pin, int64_t t) const;
  const Timeline& dut_timeline(int pin) const { return dut_[idx(pin)]; }

  BusTrace finish(std::vector<BusTransaction> transactions) const;

 private:
  struct Period {
    int64_t start;
    int64_t period;
    model::BusState bus;
  };
  static std::size_t idx(int pin) { return static_cast<std::size_t>(pin); }
  void edge(model::Edge, int64_t t);

  const spec::Spec& spec_;
  model::DeviceModel& dut_model_;
  const ElectricalModel& electrical_;
  LevelSet levels_;
  double temp_c_;
  BusPins bus_;
  std::vector<const spec::Pin*> by_num_;
  std::vector<int> input_pins_;
  int clk_pin_;
  std::vector<Timeline> tester_, dut_;
  std::vector<Period> periods_;
  const ResolvedTiming* timing_ = nullptr;
  model::Conditions cond_;
  std::optional<model::Conditions> sent_cond_;
  double derate_ = 1.0;
  uint64_t index_ = 0;
  int64_t start_ = 0;
};

// Pin-level bus-cycle decoder (min mode): ALE falling edge latches the address, the first
// RD#/WR#/INTA# strobe classifies the cycle, its rising edge (T4) closes it. A cycle with no
// strobe by the end of T3 is a halt (brief §5: an ALE pulse with no strobes).
class BusDecoder {
 public:
  struct Open {
    uint64_t t1 = 0;
    uint32_t address = 0;
    bool bhe_n = true;
    bool m_io = true;
    enum class Kind { Unknown, Read, Write, Inta } kind = Kind::Unknown;
    model::BusCycleType hint = model::BusCycleType::None;  // DeviceModel::bus_state() at T1
  };

  // Processes the DUT pin changes inside the period just clocked; returns cycles closed in it.
  std::vector<BusTransaction> end_period(const Engine&);
  // Closes an open, still strobe-less cycle as Halt (call when the model reports a halt early).
  std::vector<BusTransaction> flush(const Engine&);
  const std::optional<Open>& open() const { return open_; }

 private:
  BusTransaction close(const Engine&, uint64_t k, int64_t strobe_rise_t);

  std::optional<Open> open_;
  uint64_t last_ale_rise_ = 0;
};

uint16_t sample_ad(const Engine&, int64_t t);  // resolved AD15..0; anything but L1 reads as 0

}  // namespace i8086::tester::detail
