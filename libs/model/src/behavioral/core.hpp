#pragma once

// Behavioral 8086 core, minimum mode (design doc §4.1). Layer-private.
//
// Edge contract (see docs/decisions.md, "M3 model choices"):
//  - Every T-state starts on a CLK falling edge. clock_edge(Falling) advances the
//    BIU (T1→T2→T3→[Tw…]→T4→T1/Ti) and runs the EU for one clock.
//  - clock_edge(Rising) samples RESET, READY (T3/Tw), INTR, NMI, HOLD, MN/MX#,
//    ends the ALE pulse, commits read data, and decides the next bus cycle.
//  - Read data is latched on the falling edge that starts T4 (TDVCL/TCLDX).

#include <array>
#include <coroutine>
#include <cstdint>
#include <deque>
#include <exception>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "behavioral/task.hpp"
#include "i8086/model/device_model.hpp"

namespace i8086::model::detail {

// Die timing in integer picoseconds, read from DieProfile::timing_ns.
struct DieTiming {
  // Output delays.
  int64_t tclav;   // CLK low → address / status valid (T1, T2)
  int64_t tclaz;   // CLK low → AD float (T2 read, Ti after a write)
  int64_t tcldv;   // CLK low → write data valid (T2)
  int64_t tcllh;   // CLK low → ALE high (T1)
  int64_t tchll;   // CLK high → ALE low (T1)
  int64_t tclrl;   // CLK low → RD# low (T2)
  int64_t tclrh;   // CLK low → RD# high (T4)
  int64_t tcvctv;  // CLK low → WR#, DEN# active (T2)
  int64_t tcvctx;  // CLK low → WR#, DEN# inactive (T4)
  int64_t tchctv;  // CLK high → M/IO#, DT/R# valid (T4 or Ti before T1)
  // Input setup/hold requirements of this die.
  int64_t tdvcl, tcldx;    // read data
  int64_t tryhch, tchryx;  // READY
  int64_t tinvch;          // INTR, NMI, HOLD

  static DieTiming from(const DieProfile&);
};

struct PinMap {
  std::array<int, 16> ad{};  // AD0..AD15
  std::array<int, 4> a{};    // A16/S3..A19/S6
  int bhe_n, ale, rd_n, wr_n, m_io_n, dt_r_n, den_n, inta_n, hlda;
  int reset, ready, intr, nmi, hold, mn_mx_n;
  std::vector<int> outputs;  // every out/io pin, in spec order
  int max_pin = 0;

  static PinMap from(const spec::Spec&);
};

// Segment registers in ModR/M sreg order.
enum Sreg : uint8_t { kES = 0, kCS = 1, kSS = 2, kDS = 3 };
// S4 S3 status codes (spec decode.segment).
inline constexpr uint8_t kStatusES = 0b00, kStatusSS = 0b01, kStatusNone = 0b10,
                         kStatusDS = 0b11;

struct BusReq {
  BusCycleType type = BusCycleType::None;
  uint32_t addr = 0;     // 20-bit physical (I/O: 16-bit port)
  uint8_t status = kStatusNone;
  bool bhe_n = true;
  bool word = false;     // both lanes
  uint16_t wdata = 0;    // already placed on the lanes
  bool prefetch = false;

  bool is_read() const {
    return type == BusCycleType::CodeFetch || type == BusCycleType::MemRead ||
           type == BusCycleType::IoRead || type == BusCycleType::InterruptAck;
  }
  bool is_write() const {
    return type == BusCycleType::MemWrite || type == BusCycleType::IoWrite;
  }
};

class Core final : public DeviceModel {
 public:
  Core(const spec::Spec&, const DieProfile&, FaultSet logic_faults);
  ~Core() override;

  void set_conditions(const Conditions& c) override { conditions_ = c; }
  void set_inputs(std::span<const InputSample>) override;
  std::vector<PinEvent> clock_edge(Edge) override;
  ArchState arch_state() const override;
  BusState bus_state() const override;

 private:
  // ---- awaitables used by the EU ----
  struct ClocksAwait {
    Core* c;
    int n;
    bool await_ready() const noexcept { return n <= 0; }
    void await_suspend(std::coroutine_handle<> h) noexcept;
    void await_resume() const noexcept {}
  };
  struct QueueAwait {
    Core* c;
    bool await_ready() const noexcept { return !c->queue_.empty(); }
    void await_suspend(std::coroutine_handle<> h) noexcept;
    uint8_t await_resume();
  };
  struct BusAwait {
    Core* c;
    BusReq req;
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h) noexcept;
    uint16_t await_resume() const noexcept { return c->eu_data_; }
  };
  struct ForeverAwait {
    Core* c;
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h) noexcept;
    void await_resume() const noexcept {}
  };

  enum class Wait : uint8_t { None, Start, Clocks, Queue, Bus, Forever };

  // ---- pins (core_biu.cpp) ----
  void drive(int pin, Logic v, int64_t delay_ps);
  void drive_ad(uint16_t value, int64_t delay_ps);
  void float_ad(int64_t delay_ps);
  void drive_a(uint8_t nibble, int64_t delay_ps);
  Logic sample(int pin, int64_t setup_ps) const;
  void watch_hold(int pin, Logic* slot, int64_t hold_ps);
  int64_t half_period_ps(Edge after) const;

  void on_rising();
  void on_falling();
  void enter_reset();
  void release_reset();
  void decide_next();
  void start_t1();
  void commit_read();
  BusReq make_prefetch() const;
  int queue_free() const;
  void flush_queue();

  // ---- EU (core_eu.cpp) ----
  void eu_tick();
  Task<> eu_main();
  Task<> step();
  Task<> exec(uint8_t op);
  Task<> exec_group_ff(uint8_t modrm);
  Task<> exec_shift(uint8_t op, uint8_t modrm);
  Task<> exec_string(uint8_t op);
  Task<> interrupt(uint8_t type);

  struct Ea {
    bool is_reg = false;
    uint8_t rm = 0;
    uint8_t seg = kDS;
    uint16_t off = 0;
  };
  Task<Ea> decode_ea(uint8_t modrm);
  Task<uint16_t> read_rm(const Ea&, bool word);
  Task<> write_rm(const Ea&, bool word, uint16_t v);

  ClocksAwait clocks(int n) { return {this, n}; }
  Task<uint8_t> fetch8();
  Task<uint16_t> fetch16();
  Task<uint16_t> fetch_imm(bool word);
  Task<uint16_t> mem_read(uint8_t seg, uint16_t off, bool word);
  Task<> mem_write(uint8_t seg, uint16_t off, bool word, uint16_t v);
  Task<uint16_t> io_read(uint16_t port, bool word);
  Task<> io_write(uint16_t port, bool word, uint16_t v);
  Task<uint16_t> bus_read(BusCycleType, uint32_t addr, uint8_t status, bool word);
  Task<> bus_write(BusCycleType, uint32_t addr, uint8_t status, bool word, uint16_t v);
  Task<> push(uint16_t v);
  Task<uint16_t> pop();
  void jump(uint16_t cs, uint16_t ip);

  uint16_t reg(uint8_t r, bool word) const;
  void set_reg(uint8_t r, bool word, uint16_t v);
  uint32_t phys(uint8_t seg, uint16_t off) const;
  uint8_t status_of(uint8_t seg) const;
  bool flag(uint16_t mask) const { return (flags_ & mask) != 0; }
  void set_flag(uint16_t mask, bool on);
  void set_flags_word(uint16_t f);
  void set_szp(uint16_t r, bool word);
  uint16_t alu(uint8_t op, uint16_t a, uint16_t b, bool word);
  uint16_t inc_dec(uint16_t a, bool dec, bool word);
  bool condition(uint8_t cc) const;
  [[noreturn]] void unimplemented(const std::string& what) const;

  // ---- configuration ----
  PinMap pins_;
  DieTiming timing_;
  bool bug_inc_clears_cf_ = false;
  bool bug_shift_count_masked_ = false;
  Conditions conditions_;

  // ---- input pins ----
  struct In {
    Logic v = Logic::Z;
    int64_t changed_ps = 0;
  };
  std::vector<In> in_;
  struct HoldWatch {
    int pin;
    Logic driven;
    Logic* slot;
    int64_t hold_ps;
  };
  std::vector<HoldWatch> watches_;
  Edge last_edge_ = Edge::Falling;
  Logic nmi_prev_ = Logic::L0;

  // ---- output pins ----
  std::vector<Logic> out_;
  std::vector<PinEvent> events_;
  bool initial_reported_ = false;

  // ---- BIU ----
  bool in_reset_ = true;
  int startup_ = 0;  // rising edges left before the first bus cycle after RESET
  TState t_ = TState::Ti;
  std::optional<BusReq> cur_;
  std::optional<BusReq> next_;
  std::optional<BusReq> eu_req_;
  bool discard_ = false;    // current prefetch belongs to a flushed queue
  bool committed_ = false;  // current cycle already handed its data over (T4 rising)
  bool ad_driven_ = false;  // AD carries write data that must float in Ti
  Logic ready_latch_ = Logic::L1;
  std::array<Logic, 16> ad_latch_{};
  std::array<Logic, 16> ad_driven_at_latch_{};
  uint32_t last_addr_ = 0;
  bool last_bhe_n_ = true;
  uint64_t clk_ = 0;
  std::deque<uint8_t> queue_;
  uint16_t pf_ip_ = 0;

  // ---- EU ----
  Task<> eu_;
  Wait wait_ = Wait::None;
  std::coroutine_handle<> waiting_;
  int clocks_left_ = 0;
  bool eu_done_ = false;
  uint16_t eu_data_ = 0;
  std::exception_ptr fault_;

  std::array<uint16_t, 8> regs_{};  // AX CX DX BX SP BP SI DI
  std::array<uint16_t, 4> sregs_{};
  uint16_t ip_ = 0;
  uint16_t flags_ = 0;
  bool halted_ = false;
  bool intr_latched_ = false;
  bool inhibit_ = false;  // after MOV/POP SS: no trap check after the next instruction
  int seg_override_ = -1;
  uint8_t rep_ = 0;
};

}  // namespace i8086::model::detail
