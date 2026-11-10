// Execution unit: decode and execute the v1 instruction subset (design doc §7.3).
// EU clock counts are approximate (the datasheet has none); bus time comes from the BIU.

#include <bit>
#include <cstdio>
#include <stdexcept>

#include "behavioral/core.hpp"
#include "i8086/common/errors.hpp"

namespace i8086::model::detail {
namespace {

constexpr uint16_t kCF = 0x0001, kPF = 0x0004, kAF = 0x0010, kZF = 0x0040, kSF = 0x0080,
                   kTF = 0x0100, kIF = 0x0200, kDF = 0x0400, kOF = 0x0800;
constexpr uint8_t kAX = 0, kCX = 1, kDX = 2, kBX = 3, kSP = 4, kBP = 5, kSI = 6, kDI = 7;

uint16_t u16(uint32_t v) { return static_cast<uint16_t>(v & 0xFFFF); }
uint16_t sext8(uint8_t v) { return static_cast<uint16_t>(static_cast<int16_t>(static_cast<int8_t>(v))); }

}  // namespace

// ---------------------------------------------------------------- awaitables

void Core::ClocksAwait::await_suspend(std::coroutine_handle<> h) noexcept {
  c->clocks_left_ = n;
  c->wait_ = Wait::Clocks;
  c->waiting_ = h;
}

void Core::QueueAwait::await_suspend(std::coroutine_handle<> h) noexcept {
  c->wait_ = Wait::Queue;
  c->waiting_ = h;
}

uint8_t Core::QueueAwait::await_resume() {
  const uint8_t b = c->queue_.front();
  c->queue_.pop_front();
  c->ip_ = static_cast<uint16_t>(c->ip_ + 1);
  return b;
}

void Core::BusAwait::await_suspend(std::coroutine_handle<> h) noexcept {
  c->eu_req_ = req;
  c->eu_done_ = false;
  c->wait_ = Wait::Bus;
  c->waiting_ = h;
}

void Core::ForeverAwait::await_suspend(std::coroutine_handle<> h) noexcept {
  c->wait_ = Wait::Forever;
  c->waiting_ = h;
}

void Core::eu_tick() {
  switch (wait_) {
    case Wait::None:
    case Wait::Forever:
      return;
    case Wait::Start:
      break;
    case Wait::Clocks:
      if (--clocks_left_ > 0) return;
      break;
    case Wait::Queue:
      if (queue_.empty()) return;
      break;
    case Wait::Bus:
      if (!eu_done_) return;
      eu_done_ = false;
      break;
  }
  const std::coroutine_handle<> h = waiting_;
  wait_ = Wait::None;
  waiting_ = {};
  h.resume();
  if (eu_.done()) {
    fault_ = eu_.exception();
    if (!fault_) fault_ = std::make_exception_ptr(std::logic_error("EU exited"));
    std::rethrow_exception(fault_);
  }
}

// ---------------------------------------------------------------- helpers

uint16_t Core::reg(uint8_t r, bool word) const {
  if (word) return regs_[r];
  return r < 4 ? static_cast<uint16_t>(regs_[r] & 0xFF) : static_cast<uint16_t>(regs_[r - 4] >> 8);
}

void Core::set_reg(uint8_t r, bool word, uint16_t v) {
  if (word) {
    regs_[r] = v;
  } else if (r < 4) {
    regs_[r] = static_cast<uint16_t>((regs_[r] & 0xFF00) | (v & 0xFF));
  } else {
    uint16_t& x = regs_[r - 4];
    x = static_cast<uint16_t>((x & 0x00FF) | ((v & 0xFF) << 8));
  }
}

uint32_t Core::phys(uint8_t seg, uint16_t off) const {
  return ((static_cast<uint32_t>(sregs_[seg]) << 4) + off) & 0xFFFFF;
}

uint8_t Core::status_of(uint8_t seg) const {
  switch (seg) {
    case kES: return kStatusES;
    case kSS: return kStatusSS;
    case kDS: return kStatusDS;
    default: return kStatusNone;
  }
}

void Core::set_flag(uint16_t mask, bool on) {
  flags_ = static_cast<uint16_t>(on ? (flags_ | mask) : (flags_ & ~mask));
}

// Bits 15–12 read as 1 on the 8086, bit 1 is always 1, bits 3 and 5 are 0.
void Core::set_flags_word(uint16_t f) { flags_ = static_cast<uint16_t>((f & 0x0FD5) | 0xF002); }

void Core::set_szp(uint16_t r, bool word) {
  const uint16_t sign = word ? 0x8000 : 0x80;
  const uint16_t mask = word ? 0xFFFF : 0xFF;
  set_flag(kSF, (r & sign) != 0);
  set_flag(kZF, (r & mask) == 0);
  set_flag(kPF, (std::popcount(static_cast<unsigned>(r & 0xFF)) & 1) == 0);
}

uint16_t Core::alu(uint8_t op, uint16_t a16, uint16_t b16, bool word) {
  const uint32_t mask = word ? 0xFFFF : 0xFF;
  const uint32_t sign = word ? 0x8000 : 0x80;
  const uint32_t a = a16 & mask, b = b16 & mask;
  const uint32_t cf = flag(kCF) ? 1 : 0;
  uint32_t r = 0;
  switch (op) {
    case 0:  // ADD
    case 2:  // ADC
      r = a + b + (op == 2 ? cf : 0);
      set_flag(kCF, r > mask);
      set_flag(kOF, ((a ^ r) & (b ^ r) & sign) != 0);
      set_flag(kAF, ((a ^ b ^ r) & 0x10) != 0);
      break;
    case 3:  // SBB
    case 5:  // SUB
    case 7: {  // CMP
      const uint32_t borrow = op == 3 ? cf : 0;
      r = a - b - borrow;
      set_flag(kCF, a < b + borrow);
      set_flag(kOF, ((a ^ b) & (a ^ r) & sign) != 0);
      set_flag(kAF, ((a ^ b ^ r) & 0x10) != 0);
      break;
    }
    default:  // OR, AND, XOR
      r = op == 1 ? (a | b) : op == 4 ? (a & b) : (a ^ b);
      set_flag(kCF, false);
      set_flag(kOF, false);
      set_flag(kAF, false);
      break;
  }
  r &= mask;
  set_szp(u16(r), word);
  return op == 7 ? u16(a) : u16(r);
}

uint16_t Core::inc_dec(uint16_t a16, bool dec, bool word) {
  const uint32_t mask = word ? 0xFFFF : 0xFF;
  const uint32_t sign = word ? 0x8000 : 0x80;
  const uint32_t a = a16 & mask;
  const uint32_t r = (dec ? a - 1 : a + 1) & mask;
  set_flag(kOF, dec ? a == sign : r == sign);
  set_flag(kAF, ((a ^ 1 ^ r) & 0x10) != 0);
  set_szp(u16(r), word);
  if (!dec && bug_inc_clears_cf_) set_flag(kCF, false);
  return u16(r);
}

bool Core::condition(uint8_t cc) const {
  bool r = false;
  switch (cc >> 1) {
    case 0: r = flag(kOF); break;
    case 1: r = flag(kCF); break;
    case 2: r = flag(kZF); break;
    case 3: r = flag(kCF) || flag(kZF); break;
    case 4: r = flag(kSF); break;
    case 5: r = flag(kPF); break;
    case 6: r = flag(kSF) != flag(kOF); break;
    default: r = flag(kZF) || (flag(kSF) != flag(kOF)); break;
  }
  return (cc & 1) ? !r : r;
}

void Core::unimplemented(const std::string& what) const {
  char where[32];
  std::snprintf(where, sizeof where, " at %04X:%04X", sregs_[kCS], ip_);
  throw UnimplementedInstruction(what + where + " (outside the v1 subset, design doc §7.3)");
}

void Core::jump(uint16_t cs, uint16_t ip) {
  sregs_[kCS] = cs;
  ip_ = ip;
  flush_queue();
}

// ---------------------------------------------------------------- operand access

Task<uint8_t> Core::fetch8() { co_return co_await QueueAwait{this}; }

Task<uint16_t> Core::fetch16() {
  const uint8_t lo = co_await QueueAwait{this};
  const uint8_t hi = co_await QueueAwait{this};
  co_return static_cast<uint16_t>(lo | (hi << 8));
}

Task<uint16_t> Core::fetch_imm(bool word) {
  if (word) co_return co_await fetch16();
  co_return co_await QueueAwait{this};
}

Task<uint16_t> Core::bus_read(BusCycleType type, uint32_t addr, uint8_t status, bool word) {
  BusReq r;
  r.type = type;
  r.addr = addr;
  r.status = status;
  r.word = word;
  r.bhe_n = !(word || (addr & 1U));
  co_return co_await BusAwait{this, r};
}

Task<> Core::bus_write(BusCycleType type, uint32_t addr, uint8_t status, bool word, uint16_t v) {
  BusReq r;
  r.type = type;
  r.addr = addr;
  r.status = status;
  r.word = word;
  r.bhe_n = !(word || (addr & 1U));
  // A byte is driven on both lanes; the BHE#/A0 decode says which one is valid.
  r.wdata = word ? v : static_cast<uint16_t>((v & 0xFF) * 0x0101);
  co_await BusAwait{this, r};
}

// A word at an odd address takes two byte cycles; the offset wraps inside the segment.
Task<uint16_t> Core::mem_read(uint8_t seg, uint16_t off, bool word) {
  const uint8_t st = status_of(seg);
  if (!word) co_return co_await bus_read(BusCycleType::MemRead, phys(seg, off), st, false);
  if ((off & 1U) == 0) co_return co_await bus_read(BusCycleType::MemRead, phys(seg, off), st, true);
  const uint16_t lo = co_await bus_read(BusCycleType::MemRead, phys(seg, off), st, false);
  const uint16_t hi = co_await bus_read(BusCycleType::MemRead, phys(seg, u16(off + 1U)), st, false);
  co_return static_cast<uint16_t>(lo | (hi << 8));
}

Task<> Core::mem_write(uint8_t seg, uint16_t off, bool word, uint16_t v) {
  const uint8_t st = status_of(seg);
  if (!word) {
    co_await bus_write(BusCycleType::MemWrite, phys(seg, off), st, false, v);
  } else if ((off & 1U) == 0) {
    co_await bus_write(BusCycleType::MemWrite, phys(seg, off), st, true, v);
  } else {
    co_await bus_write(BusCycleType::MemWrite, phys(seg, off), st, false, v);
    co_await bus_write(BusCycleType::MemWrite, phys(seg, u16(off + 1U)), st, false,
                       static_cast<uint16_t>(v >> 8));
  }
}

Task<uint16_t> Core::io_read(uint16_t port, bool word) {
  if (!word || (port & 1U) == 0) co_return co_await bus_read(BusCycleType::IoRead, port, kStatusNone, word);
  const uint16_t lo = co_await bus_read(BusCycleType::IoRead, port, kStatusNone, false);
  const uint16_t hi = co_await bus_read(BusCycleType::IoRead, u16(port + 1U), kStatusNone, false);
  co_return static_cast<uint16_t>(lo | (hi << 8));
}

Task<> Core::io_write(uint16_t port, bool word, uint16_t v) {
  if (!word || (port & 1U) == 0) {
    co_await bus_write(BusCycleType::IoWrite, port, kStatusNone, word, v);
  } else {
    co_await bus_write(BusCycleType::IoWrite, port, kStatusNone, false, v);
    co_await bus_write(BusCycleType::IoWrite, u16(port + 1U), kStatusNone, false,
                       static_cast<uint16_t>(v >> 8));
  }
}

Task<> Core::push(uint16_t v) {
  regs_[kSP] = u16(regs_[kSP] - 2U);
  co_await mem_write(kSS, regs_[kSP], true, v);
}

Task<uint16_t> Core::pop() {
  const uint16_t v = co_await mem_read(kSS, regs_[kSP], true);
  regs_[kSP] = u16(regs_[kSP] + 2U);
  co_return v;
}

Task<Core::Ea> Core::decode_ea(uint8_t modrm) {
  Ea ea;
  const uint8_t mod = modrm >> 6;
  ea.rm = modrm & 7;
  if (mod == 3) {
    ea.is_reg = true;
    co_return ea;
  }
  int clk = 0;
  uint32_t off = 0;
  ea.seg = kDS;
  switch (ea.rm) {
    case 0: off = uint32_t{regs_[kBX]} + regs_[kSI]; clk = 7; break;
    case 1: off = uint32_t{regs_[kBX]} + regs_[kDI]; clk = 8; break;
    case 2: off = uint32_t{regs_[kBP]} + regs_[kSI]; clk = 8; ea.seg = kSS; break;
    case 3: off = uint32_t{regs_[kBP]} + regs_[kDI]; clk = 7; ea.seg = kSS; break;
    case 4: off = regs_[kSI]; clk = 5; break;
    case 5: off = regs_[kDI]; clk = 5; break;
    case 6:
      if (mod == 0) {
        off = co_await fetch16();
        clk = 6;
      } else {
        off = regs_[kBP];
        clk = 5;
        ea.seg = kSS;
      }
      break;
    default: off = regs_[kBX]; clk = 5; break;
  }
  if (mod == 1) {
    off += sext8(co_await fetch8());
    clk += 4;
  } else if (mod == 2) {
    off += co_await fetch16();
    clk += 4;
  }
  if (seg_override_ >= 0) {
    ea.seg = static_cast<uint8_t>(seg_override_);
    clk += 2;
  }
  ea.off = u16(off);
  co_await clocks(clk);
  co_return ea;
}

Task<uint16_t> Core::read_rm(const Ea& ea, bool word) {
  if (ea.is_reg) co_return reg(ea.rm, word);
  co_return co_await mem_read(ea.seg, ea.off, word);
}

Task<> Core::write_rm(const Ea& ea, bool word, uint16_t v) {
  if (ea.is_reg) {
    set_reg(ea.rm, word, v);
    co_return;
  }
  co_await mem_write(ea.seg, ea.off, word, v);
}

// ---------------------------------------------------------------- control

Task<> Core::eu_main() {
  for (;;) co_await step();
}

Task<> Core::step() {
  if (intr_latched_ && flag(kIF)) throw NotImplemented("INTR/INTA [M8]");
  const bool trap = flag(kTF);
  inhibit_ = false;
  seg_override_ = -1;
  rep_ = 0;
  uint8_t op = co_await fetch8();
  for (;;) {
    if (op == 0x26 || op == 0x2E || op == 0x36 || op == 0x3E) {
      seg_override_ = (op >> 3) & 3;
    } else if (op == 0xF2 || op == 0xF3) {
      rep_ = op;
    } else {
      break;
    }
    co_await clocks(2);
    op = co_await fetch8();
  }
  co_await exec(op);
  if (trap && !inhibit_) co_await interrupt(1);
}

// Brief §5: push FLAGS, clear IF and TF, push CS, push IP, load IP and CS from type×4.
Task<> Core::interrupt(uint8_t type) {
  co_await clocks(20);
  co_await push(flags_);
  set_flag(kIF, false);
  set_flag(kTF, false);
  co_await push(sregs_[kCS]);
  co_await push(ip_);
  const uint32_t vec = uint32_t{type} * 4;
  const uint16_t ip = co_await bus_read(BusCycleType::MemRead, vec, kStatusNone, true);
  const uint16_t cs = co_await bus_read(BusCycleType::MemRead, vec + 2, kStatusNone, true);
  jump(cs, ip);
}

Task<> Core::exec(uint8_t op) {
  const bool w = (op & 1) != 0;

  // ALU r/m,reg / reg,r/m / acc,imm: ADD OR ADC SBB AND SUB XOR CMP.
  if (op < 0x40 && (op & 7) < 6) {
    const auto alu_op = static_cast<uint8_t>((op >> 3) & 7);
    const uint8_t form = op & 7;
    if (form >= 4) {
      const uint16_t imm = co_await fetch_imm(w);
      const uint16_t r = alu(alu_op, reg(kAX, w), imm, w);
      if (alu_op != 7) set_reg(kAX, w, r);
      co_await clocks(4);
      co_return;
    }
    const uint8_t modrm = co_await fetch8();
    const Ea ea = co_await decode_ea(modrm);
    const auto r = static_cast<uint8_t>((modrm >> 3) & 7);
    const uint16_t rm = co_await read_rm(ea, w);
    if (form < 2) {
      const uint16_t res = alu(alu_op, rm, reg(r, w), w);
      if (alu_op != 7) co_await write_rm(ea, w, res);
    } else {
      const uint16_t res = alu(alu_op, reg(r, w), rm, w);
      if (alu_op != 7) set_reg(r, w, res);
    }
    co_await clocks(3);
    co_return;
  }

  switch (op) {
    // ---- segment register push/pop
    case 0x06: case 0x0E: case 0x16: case 0x1E:
      co_await clocks(6);
      co_await push(sregs_[(op >> 3) & 3]);
      co_return;
    case 0x07: case 0x17: case 0x1F: {
      const uint16_t v = co_await pop();
      sregs_[(op >> 3) & 3] = v;
      if (op == 0x17) inhibit_ = true;
      co_await clocks(4);
      co_return;
    }
    case 0x0F: {  // POP CS (8086 only; D11). Execution continues at the new CS:IP.
      const uint16_t v = co_await pop();
      co_await clocks(4);
      jump(v, ip_);
      co_return;
    }

    // ---- INC/DEC/PUSH/POP reg16
    case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47:
    case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4E: case 0x4F: {
      const uint8_t r = op & 7;
      regs_[r] = inc_dec(regs_[r], op >= 0x48, true);
      co_await clocks(2);
      co_return;
    }
    case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57: {
      const uint8_t r = op & 7;
      // PUSH SP stores the decremented value on the 8086.
      const uint16_t v = r == kSP ? u16(regs_[kSP] - 2U) : regs_[r];
      co_await clocks(7);
      co_await push(v);
      co_return;
    }
    case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F: {
      const uint16_t v = co_await pop();
      regs_[op & 7] = v;
      co_await clocks(4);
      co_return;
    }

    // ---- Jcc rel8
    case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x77:
    case 0x78: case 0x79: case 0x7A: case 0x7B: case 0x7C: case 0x7D: case 0x7E: case 0x7F: {
      const uint16_t rel = sext8(co_await fetch8());
      if (condition(op & 0x0F)) {
        co_await clocks(12);
        jump(sregs_[kCS], u16(ip_ + rel));
      } else {
        co_await clocks(4);
      }
      co_return;
    }

    // ---- ALU r/m, imm
    case 0x80: case 0x81: case 0x82: case 0x83: {
      const uint8_t modrm = co_await fetch8();
      const Ea ea = co_await decode_ea(modrm);
      uint16_t imm = co_await fetch_imm(op == 0x81);
      if (op == 0x83) imm = sext8(static_cast<uint8_t>(imm));
      const auto alu_op = static_cast<uint8_t>((modrm >> 3) & 7);
      const uint16_t v = co_await read_rm(ea, w);
      const uint16_t r = alu(alu_op, v, imm, w);
      if (alu_op != 7) co_await write_rm(ea, w, r);
      co_await clocks(4);
      co_return;
    }

    // ---- MOV
    case 0x88: case 0x89: case 0x8A: case 0x8B: {
      const uint8_t modrm = co_await fetch8();
      const Ea ea = co_await decode_ea(modrm);
      const auto r = static_cast<uint8_t>((modrm >> 3) & 7);
      if (op < 0x8A) {
        co_await write_rm(ea, w, reg(r, w));
      } else {
        set_reg(r, w, co_await read_rm(ea, w));
      }
      co_await clocks(2);
      co_return;
    }
    case 0x8C: {
      const uint8_t modrm = co_await fetch8();
      const Ea ea = co_await decode_ea(modrm);
      co_await write_rm(ea, true, sregs_[(modrm >> 3) & 3]);
      co_await clocks(2);
      co_return;
    }
    case 0x8E: {
      const uint8_t modrm = co_await fetch8();
      const Ea ea = co_await decode_ea(modrm);
      const uint16_t v = co_await read_rm(ea, true);
      const auto s = static_cast<uint8_t>((modrm >> 3) & 3);
      co_await clocks(2);
      if (s == kCS) {
        jump(v, ip_);
      } else {
        sregs_[s] = v;
        if (s == kSS) inhibit_ = true;
      }
      co_return;
    }
    case 0x8F: {
      const uint8_t modrm = co_await fetch8();
      if (((modrm >> 3) & 7) != 0) unimplemented("8F /" + std::to_string((modrm >> 3) & 7));
      const Ea ea = co_await decode_ea(modrm);
      const uint16_t v = co_await pop();
      co_await write_rm(ea, true, v);
      co_await clocks(4);
      co_return;
    }
    case 0x90:  // NOP
      co_await clocks(3);
      co_return;

    // ---- far CALL, PUSHF, POPF
    case 0x9A: {
      const uint16_t off = co_await fetch16();
      const uint16_t seg = co_await fetch16();
      co_await clocks(16);
      co_await push(sregs_[kCS]);
      co_await push(ip_);
      jump(seg, off);
      co_return;
    }
    case 0x9C:
      co_await clocks(6);
      co_await push(flags_);
      co_return;
    case 0x9D:
      set_flags_word(co_await pop());
      co_await clocks(4);
      co_return;

    // ---- MOV acc, moffs
    case 0xA0: case 0xA1: case 0xA2: case 0xA3: {
      const uint16_t off = co_await fetch16();
      const auto seg = static_cast<uint8_t>(seg_override_ >= 0 ? seg_override_ : kDS);
      if (op < 0xA2) {
        set_reg(kAX, w, co_await mem_read(seg, off, w));
      } else {
        co_await mem_write(seg, off, w, reg(kAX, w));
      }
      co_await clocks(6);
      co_return;
    }

    // ---- strings
    case 0xA4: case 0xA5: case 0xAA: case 0xAB: {
      const auto delta = static_cast<uint16_t>(flag(kDF) ? -(w ? 2 : 1) : (w ? 2 : 1));
      const bool movs = op < 0xA8;
      const auto src = static_cast<uint8_t>(seg_override_ >= 0 ? seg_override_ : kDS);
      if (rep_ != 0) co_await clocks(9);
      while (rep_ == 0 || regs_[kCX] != 0) {
        if (movs) {
          const uint16_t v = co_await mem_read(src, regs_[kSI], w);
          co_await mem_write(kES, regs_[kDI], w, v);
          regs_[kSI] = u16(regs_[kSI] + delta);
        } else {
          co_await mem_write(kES, regs_[kDI], w, reg(kAX, w));
        }
        regs_[kDI] = u16(regs_[kDI] + delta);
        co_await clocks(movs ? 9 : 6);
        if (rep_ == 0) break;
        regs_[kCX] = u16(regs_[kCX] - 1U);
      }
      co_return;
    }

    // ---- MOV reg, imm
    case 0xB0: case 0xB1: case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: case 0xB7:
      set_reg(op & 7, false, co_await fetch8());
      co_await clocks(4);
      co_return;
    case 0xB8: case 0xB9: case 0xBA: case 0xBB: case 0xBC: case 0xBD: case 0xBE: case 0xBF:
      regs_[op & 7] = co_await fetch16();
      co_await clocks(4);
      co_return;

    // ---- RET
    case 0xC2: case 0xC3: {
      uint16_t imm = 0;
      if (op == 0xC2) imm = co_await fetch16();
      const uint16_t ip = co_await pop();
      regs_[kSP] = u16(regs_[kSP] + imm);
      co_await clocks(8);
      jump(sregs_[kCS], ip);
      co_return;
    }
    case 0xCA: case 0xCB: {
      uint16_t imm = 0;
      if (op == 0xCA) imm = co_await fetch16();
      const uint16_t ip = co_await pop();
      const uint16_t cs = co_await pop();
      regs_[kSP] = u16(regs_[kSP] + imm);
      co_await clocks(10);
      jump(cs, ip);
      co_return;
    }

    // ---- MOV r/m, imm
    case 0xC6: case 0xC7: {
      const uint8_t modrm = co_await fetch8();
      if (((modrm >> 3) & 7) != 0) unimplemented("C6/C7 /" + std::to_string((modrm >> 3) & 7));
      const Ea ea = co_await decode_ea(modrm);
      const uint16_t imm = co_await fetch_imm(w);
      co_await write_rm(ea, w, imm);
      co_await clocks(4);
      co_return;
    }

    // ---- INT, INTO, IRET
    case 0xCC:
      co_await interrupt(3);
      co_return;
    case 0xCD: {
      const uint8_t type = co_await fetch8();
      co_await interrupt(type);
      co_return;
    }
    case 0xCE:
      if (flag(kOF)) {
        co_await interrupt(4);
      } else {
        co_await clocks(4);
      }
      co_return;
    case 0xCF: {
      const uint16_t ip = co_await pop();
      const uint16_t cs = co_await pop();
      set_flags_word(co_await pop());
      co_await clocks(12);
      jump(cs, ip);
      co_return;
    }

    // ---- shifts
    case 0xD0: case 0xD1: case 0xD2: case 0xD3:
    {
      const uint8_t modrm = co_await fetch8();
      co_await exec_shift(op, modrm);
      co_return;
    }

    case 0xD6:  // undocumented SALC (D11): AL = CF ? FFh : 00h, no trap
      set_reg(kAX, false, flag(kCF) ? 0xFF : 0x00);
      co_await clocks(3);
      co_return;

    // ---- IN/OUT
    case 0xE4: case 0xE5: case 0xE6: case 0xE7:
    case 0xEC: case 0xED: case 0xEE: case 0xEF: {
      uint16_t port = regs_[kDX];
      if (op < 0xEC) port = co_await fetch_imm(false);
      co_await clocks(6);
      if ((op & 2) == 0) {
        set_reg(kAX, w, co_await io_read(port, w));
      } else {
        co_await io_write(port, w, reg(kAX, w));
      }
      co_return;
    }

    // ---- CALL/JMP
    case 0xE8: {
      const uint16_t rel = co_await fetch16();
      co_await clocks(11);
      co_await push(ip_);
      jump(sregs_[kCS], u16(ip_ + rel));
      co_return;
    }
    case 0xE9: {
      const uint16_t rel = co_await fetch16();
      co_await clocks(11);
      jump(sregs_[kCS], u16(ip_ + rel));
      co_return;
    }
    case 0xEA: {
      const uint16_t off = co_await fetch16();
      const uint16_t seg = co_await fetch16();
      co_await clocks(11);
      jump(seg, off);
      co_return;
    }
    case 0xEB: {
      const uint16_t rel = sext8(co_await fetch8());
      co_await clocks(11);
      jump(sregs_[kCS], u16(ip_ + rel));
      co_return;
    }

    // ---- HLT: min-mode halt cycle (ALE pulse, no strobes); the BIU then idles until RESET
    case 0xF4: {
      BusReq r;
      r.type = BusCycleType::Halt;
      r.addr = phys(kCS, ip_);
      co_await BusAwait{this, r};
      co_await ForeverAwait{this};
      co_return;
    }

    // ---- F6/F7: DIV only
    case 0xF6: case 0xF7: {
      const uint8_t modrm = co_await fetch8();
      const auto sub = (modrm >> 3) & 7;
      if (sub != 6) unimplemented("F6/F7 /" + std::to_string(sub));
      const Ea ea = co_await decode_ea(modrm);
      const uint16_t d = co_await read_rm(ea, w);
      co_await clocks(w ? 140 : 76);
      if (w) {
        const uint32_t n = (uint32_t{regs_[kDX]} << 16) | regs_[kAX];
        if (d == 0 || n / d > 0xFFFF) {
          co_await interrupt(0);
          co_return;
        }
        regs_[kAX] = u16(n / d);
        regs_[kDX] = u16(n % d);
      } else {
        const uint32_t n = regs_[kAX];
        const uint32_t dv = d & 0xFF;
        if (dv == 0 || n / dv > 0xFF) {
          co_await interrupt(0);
          co_return;
        }
        regs_[kAX] = static_cast<uint16_t>(((n % dv) << 8) | (n / dv));
      }
      co_return;
    }

    // ---- flag control
    case 0xF8: set_flag(kCF, false); co_await clocks(2); co_return;
    case 0xF9: set_flag(kCF, true); co_await clocks(2); co_return;
    case 0xFA: set_flag(kIF, false); co_await clocks(2); co_return;
    case 0xFB: set_flag(kIF, true); co_await clocks(2); co_return;
    case 0xFC: set_flag(kDF, false); co_await clocks(2); co_return;
    case 0xFD: set_flag(kDF, true); co_await clocks(2); co_return;

    // ---- FE/FF groups
    case 0xFE: {
      const uint8_t modrm = co_await fetch8();
      const auto sub = (modrm >> 3) & 7;
      if (sub > 1) unimplemented("FE /" + std::to_string(sub));
      const Ea ea = co_await decode_ea(modrm);
      const uint16_t v = co_await read_rm(ea, false);
      co_await write_rm(ea, false, inc_dec(v, sub == 1, false));
      co_await clocks(3);
      co_return;
    }
    case 0xFF: {
      const uint8_t modrm = co_await fetch8();
      co_await exec_group_ff(modrm);
      co_return;
    }

    default:
      break;
  }
  char buf[16];
  std::snprintf(buf, sizeof buf, "opcode %02Xh", op);
  unimplemented(buf);
}

Task<> Core::exec_group_ff(uint8_t modrm) {
  const auto sub = (modrm >> 3) & 7;
  if (sub == 7) unimplemented("FF /7");
  if ((sub == 3 || sub == 5) && (modrm >> 6) == 3) unimplemented("far CALL/JMP with a register operand");
  const Ea ea = co_await decode_ea(modrm);
  switch (sub) {
    case 0:
    case 1: {
      const uint16_t v = co_await read_rm(ea, true);
      co_await write_rm(ea, true, inc_dec(v, sub == 1, true));
      co_await clocks(3);
      break;
    }
    case 2:
    case 4: {
      const uint16_t target = co_await read_rm(ea, true);
      co_await clocks(11);
      if (sub == 2) co_await push(ip_);
      jump(sregs_[kCS], target);
      break;
    }
    case 3:
    case 5: {
      const uint16_t off = co_await mem_read(ea.seg, ea.off, true);
      const uint16_t seg = co_await mem_read(ea.seg, u16(ea.off + 2U), true);
      co_await clocks(16);
      if (sub == 3) {
        co_await push(sregs_[kCS]);
        co_await push(ip_);
      }
      jump(seg, off);
      break;
    }
    default: {  // 6: PUSH r/m16
      const uint16_t v = co_await read_rm(ea, true);
      co_await clocks(7);
      co_await push(v);
      break;
    }
  }
}

// SHL/SAL, SHR, SAR by 1 or CL. The count is not masked on the 8086 (D8). For counts
// above 1 OF follows the last single-bit step; AF is left unchanged (undefined).
Task<> Core::exec_shift(uint8_t op, uint8_t modrm) {
  const bool w = (op & 1) != 0;
  const auto sub = (modrm >> 3) & 7;
  if (sub != 4 && sub != 5 && sub != 7) unimplemented("shift/rotate group /" + std::to_string(sub));
  const Ea ea = co_await decode_ea(modrm);
  uint32_t count = op >= 0xD2 ? (regs_[kCX] & 0xFFU) : 1U;
  if (bug_shift_count_masked_) count &= 0x1F;
  uint32_t v = co_await read_rm(ea, w);
  co_await clocks(static_cast<int>(8 + 4 * count));
  if (count == 0) co_return;
  const uint32_t mask = w ? 0xFFFF : 0xFF;
  const uint32_t sign = w ? 0x8000 : 0x80;
  bool cf = false, of = false;
  for (uint32_t i = 0; i < count; ++i) {
    if (sub == 4) {
      cf = (v & sign) != 0;
      v = (v << 1) & mask;
      of = ((v & sign) != 0) != cf;
    } else if (sub == 5) {
      cf = (v & 1U) != 0;
      of = (v & sign) != 0;
      v >>= 1;
    } else {
      cf = (v & 1U) != 0;
      v = (v >> 1) | (v & sign);
      of = false;
    }
  }
  set_flag(kCF, cf);
  set_flag(kOF, of);
  set_szp(u16(v), w);
  co_await write_rm(ea, w, u16(v));
}

}  // namespace i8086::model::detail
