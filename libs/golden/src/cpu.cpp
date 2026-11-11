#include "cpu.hpp"

#include <bit>
#include <utility>

namespace i8086::golden::detail {

namespace {

// Thrown before an instruction changes any state; run() rewinds IP and stops.
struct Unimplemented {};

constexpr uint32_t kMemSize = 1u << 20;
constexpr uint32_t kAddrMask = kMemSize - 1;
// 8086 prefixes have no length limit, but a run of prefixes this long can only be a runaway.
constexpr int kMaxPrefixes = 0x10000;

constexpr uint32_t mask_of(bool word) { return word ? 0xFFFFu : 0xFFu; }
constexpr uint32_t msb_of(bool word) { return word ? 0x8000u : 0x80u; }
constexpr uint16_t lo16(uint32_t v) { return static_cast<uint16_t>(v & 0xFFFFu); }
constexpr uint8_t lo8(uint32_t v) { return static_cast<uint8_t>(v & 0xFFu); }
constexpr uint16_t sext8(uint8_t v) { return static_cast<uint16_t>(static_cast<int8_t>(v)); }

}  // namespace

Cpu::Cpu(const Program& program, Result& out)
    : mem_(kMemSize, 0), program_(program), out_(out) {
  for (const auto& [addr, bytes] : program.segments) {
    for (size_t i = 0; i < bytes.size(); ++i) {
      mem_[(addr + i) & kAddrMask] = bytes[i];
    }
  }
  const Registers reset = reset_registers();
  r_ = {reset.ax, reset.cx, reset.dx, reset.bx, reset.sp, reset.bp, reset.si, reset.di};
  s_ = {reset.es, reset.cs, reset.ss, reset.ds};
  ip_ = reset.ip;
  flags_ = static_cast<uint16_t>(reset.flags & kFlagsMask);
  for (const auto& r : program.interrupts) requests_.push_back({r});
}

void Cpu::run(uint64_t max_instructions) {
  out_.stop = StopReason::InstructionLimit;
  while (out_.instructions < max_instructions) {
    try {
      if (step() == Step::Halt) {
        out_.stop = StopReason::Halt;
        break;
      }
    } catch (const Unimplemented&) {
      ip_ = start_ip_;
      out_.stop = StopReason::Unimplemented;
      break;
    }
  }
  Registers& g = out_.regs;
  g.ax = r_[AX], g.cx = r_[CX], g.dx = r_[DX], g.bx = r_[BX];
  g.sp = r_[SP], g.bp = r_[BP], g.si = r_[SI], g.di = r_[DI];
  g.es = s_[ES], g.cs = s_[CS], g.ss = s_[SS], g.ds = s_[DS];
  g.ip = ip_;
  g.flags = flags_;
}

Cpu::Step Cpu::step() {
  if (!std::exchange(shadow_, false)) take_external();
  start_ip_ = ip_;
  seg_override_ = -1;
  rep_ = 0;
  interrupted_ = false;
  inhibit_trap_ = false;
  const bool trap = flag(kTF);

  uint8_t op = fetch8();
  for (int n = 0;; ++n) {
    if (n == kMaxPrefixes) throw Unimplemented{};
    if (op == 0x26 || op == 0x2E || op == 0x36 || op == 0x3E) {
      seg_override_ = (op >> 3) & 3;
    } else if (op == 0xF2 || op == 0xF3) {
      rep_ = op;
    } else if (op != 0xF0 && op != 0xF1) {  // LOCK (F1 is its undocumented alias)
      break;
    }
    op = fetch8();
  }

  if (op == 0xF4) {  // HLT: IP is left past the HLT, as on the chip.
    ++out_.instructions;
    return wake_from_halt() ? Step::Continue : Step::Halt;
  }
  execute(op);
  if (inhibit_trap_) shadow_ = true;
  if (trap && !interrupted_ && !inhibit_trap_) interrupt(1);
  ++out_.instructions;
  return Step::Continue;
}

// ---- memory, stack and registers -------------------------------------------------------------

uint8_t Cpu::fetch8() {
  const uint8_t v = rd8(s_[CS], ip_);
  ip_ = lo16(ip_ + 1u);
  return v;
}

uint16_t Cpu::fetch16() {
  const uint8_t lo = fetch8();
  return static_cast<uint16_t>(lo | (fetch8() << 8));
}

uint8_t Cpu::rd8(uint16_t seg, uint16_t off) const {
  return mem_[((static_cast<uint32_t>(seg) << 4) + off) & kAddrMask];
}

uint16_t Cpu::rd16(uint16_t seg, uint16_t off) const {
  // The offset wraps inside the segment: a word at xxxx:FFFF takes its high byte from xxxx:0000.
  return static_cast<uint16_t>(rd8(seg, off) | (rd8(seg, lo16(off + 1u)) << 8));
}

void Cpu::wr8(uint16_t seg, uint16_t off, uint8_t v) {
  const uint32_t addr = ((static_cast<uint32_t>(seg) << 4) + off) & kAddrMask;
  mem_[addr] = v;
  out_.mem_writes.push_back({addr, v});
}

void Cpu::wr16(uint16_t seg, uint16_t off, uint16_t v) {
  wr8(seg, off, lo8(v));
  wr8(seg, lo16(off + 1u), lo8(v >> 8));
}

void Cpu::push(uint16_t v) {
  r_[SP] = lo16(r_[SP] - 2u);
  wr16(s_[SS], r_[SP], v);
}

uint16_t Cpu::pop() {
  const uint16_t v = rd16(s_[SS], r_[SP]);
  r_[SP] = lo16(r_[SP] + 2u);
  return v;
}

uint8_t Cpu::reg8(unsigned n) const {
  return n < 4 ? lo8(r_[n]) : lo8(r_[n - 4] >> 8);
}

void Cpu::set_reg8(unsigned n, uint8_t v) {
  if (n < 4) {
    r_[n] = static_cast<uint16_t>((r_[n] & 0xFF00u) | v);
  } else {
    r_[n - 4] = static_cast<uint16_t>((r_[n - 4] & 0x00FFu) | (v << 8));
  }
}

uint16_t Cpu::data_seg(Seg dflt) const {
  const int seg = seg_override_ >= 0 ? seg_override_ : dflt;
  return s_[static_cast<unsigned>(seg)];
}

uint32_t Cpu::reg_op(unsigned n, bool word) const { return word ? r_[n] : reg8(n); }

void Cpu::set_reg_op(unsigned n, bool word, uint32_t v) {
  if (word) {
    r_[n] = lo16(v);
  } else {
    set_reg8(n, lo8(v));
  }
}

Cpu::Operand Cpu::modrm() {
  const uint8_t b = fetch8();
  Operand o;
  const unsigned mod = b >> 6;
  o.reg = (b >> 3) & 7;
  o.rm = b & 7;
  if (mod == 3) return o;

  o.is_mem = true;
  uint16_t ea = 0;
  Seg dflt = DS;
  switch (o.rm) {
    case 0: ea = lo16(0u + r_[BX] + r_[SI]); break;
    case 1: ea = lo16(0u + r_[BX] + r_[DI]); break;
    case 2: ea = lo16(0u + r_[BP] + r_[SI]); dflt = SS; break;
    case 3: ea = lo16(0u + r_[BP] + r_[DI]); dflt = SS; break;
    case 4: ea = r_[SI]; break;
    case 5: ea = r_[DI]; break;
    case 6:
      if (mod == 0) {
        ea = fetch16();
      } else {
        ea = r_[BP];
        dflt = SS;
      }
      break;
    default: ea = r_[BX]; break;
  }
  if (mod == 1) {
    ea = lo16(ea + sext8(fetch8()));
  } else if (mod == 2) {
    ea = lo16(ea + fetch16());
  }
  o.off = ea;
  o.seg = data_seg(dflt);
  return o;
}

uint32_t Cpu::read_op(const Operand& o, bool word) const {
  if (!o.is_mem) return reg_op(o.rm, word);
  return word ? rd16(o.seg, o.off) : rd8(o.seg, o.off);
}

void Cpu::write_op(const Operand& o, bool word, uint32_t v) {
  if (!o.is_mem) {
    set_reg_op(o.rm, word, v);
  } else if (word) {
    wr16(o.seg, o.off, lo16(v));
  } else {
    wr8(o.seg, o.off, lo8(v));
  }
}

// ---- flags and ALU ---------------------------------------------------------------------------

void Cpu::set_flag(uint16_t f, bool on) {
  flags_ = static_cast<uint16_t>(on ? (flags_ | f) : (flags_ & ~f));
}

void Cpu::set_szp(uint32_t res, bool word) {
  res &= mask_of(word);
  set_flag(kZF, res == 0);
  set_flag(kSF, (res & msb_of(word)) != 0);
  set_flag(kPF, std::popcount(res & 0xFFu) % 2 == 0);
}

// op is the ALU field of opcodes 00-3F and of group 1: ADD OR ADC SBB AND SUB XOR CMP.
uint32_t Cpu::alu(unsigned op, uint32_t a, uint32_t b, bool word) {
  const uint32_t mask = mask_of(word), msb = msb_of(word);
  uint32_t res = 0;
  switch (op) {
    case 0:
    case 2: {
      const uint32_t c = (op == 2 && flag(kCF)) ? 1 : 0;
      res = a + b + c;
      set_flag(kCF, res > mask);
      set_flag(kOF, ((a ^ res) & (b ^ res) & msb) != 0);
      set_flag(kAF, ((a ^ b ^ res) & 0x10) != 0);
      break;
    }
    case 3:
    case 5:
    case 7: {
      const uint32_t c = (op == 3 && flag(kCF)) ? 1 : 0;
      res = a - b - c;
      set_flag(kCF, a < b + c);
      set_flag(kOF, ((a ^ b) & (a ^ res) & msb) != 0);
      set_flag(kAF, ((a ^ b ^ res) & 0x10) != 0);
      break;
    }
    default:
      res = op == 1 ? (a | b) : op == 4 ? (a & b) : (a ^ b);
      set_flag(kCF, false);
      set_flag(kOF, false);
      break;
  }
  res &= mask;
  set_szp(res, word);
  return res;
}

uint32_t Cpu::inc_dec(uint32_t v, bool dec, bool word) {
  const bool cf = flag(kCF);
  const uint32_t res = alu(dec ? 5 : 0, v, 1, word);
  set_flag(kCF, cf);
  return res;
}

// op is the group-2 reg field: ROL ROR RCL RCR SHL SHR (6 = undocumented) SAR.
// The count is not masked on the 8086 (brief §2, D8).
uint32_t Cpu::shift(unsigned op, uint32_t v, unsigned count, bool word) {
  if (op == 6) throw Unimplemented{};
  if (count == 0) return v;
  const uint32_t mask = mask_of(word), msb = msb_of(word);
  const uint32_t original = v;
  bool cf = flag(kCF);
  for (unsigned i = 0; i < count; ++i) {
    const bool out_hi = (v & msb) != 0, out_lo = (v & 1) != 0;
    switch (op) {
      case 0: v = ((v << 1) | (out_hi ? 1 : 0)) & mask; cf = out_hi; break;
      case 1: v = (v >> 1) | (out_lo ? msb : 0); cf = out_lo; break;
      case 2: v = ((v << 1) | (cf ? 1 : 0)) & mask; cf = out_hi; break;
      case 3: v = (v >> 1) | (cf ? msb : 0); cf = out_lo; break;
      case 4: v = (v << 1) & mask; cf = out_hi; break;
      case 5: v >>= 1; cf = out_lo; break;
      default: v = (v >> 1) | (v & msb); cf = out_lo; break;
    }
  }
  set_flag(kCF, cf);
  if (count == 1) {
    const bool res_hi = (v & msb) != 0;
    switch (op) {
      case 0:
      case 2:
      case 4: set_flag(kOF, res_hi != cf); break;
      case 1:
      case 3: set_flag(kOF, res_hi != ((v & (msb >> 1)) != 0)); break;
      case 5: set_flag(kOF, (original & msb) != 0); break;
      default: set_flag(kOF, false); break;
    }
  }
  if (op >= 4) set_szp(v, word);
  return v;
}

bool Cpu::condition(unsigned cc) const {
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

// Returns false on a divide error (divisor 0 or quotient out of range); nothing is written then.
bool Cpu::divide(const Operand& o, bool word, bool is_signed) {
  const uint32_t src = read_op(o, word);
  if (src == 0) return false;
  if (!word) {
    if (is_signed) {
      const int32_t n = static_cast<int16_t>(r_[AX]);
      const int32_t d = static_cast<int8_t>(lo8(src));
      const int32_t q = n / d, m = n % d;
      // The 8086 range is -127..127: a quotient of -128 also raises type 0.
      if (q > 127 || q < -127) return false;
      set_reg8(0, lo8(static_cast<uint32_t>(q)));
      set_reg8(4, lo8(static_cast<uint32_t>(m)));
    } else {
      const uint32_t n = r_[AX];
      const uint32_t q = n / src;
      if (q > 0xFF) return false;
      set_reg8(0, lo8(q));
      set_reg8(4, lo8(n % src));
    }
    return true;
  }
  const uint32_t n = (static_cast<uint32_t>(r_[DX]) << 16) | r_[AX];
  if (is_signed) {
    const int64_t sn = static_cast<int32_t>(n);
    const int64_t d = static_cast<int16_t>(lo16(src));
    const int64_t q = sn / d, m = sn % d;
    if (q > 32767 || q < -32767) return false;
    r_[AX] = lo16(static_cast<uint32_t>(q));
    r_[DX] = lo16(static_cast<uint32_t>(m));
  } else {
    const uint32_t q = n / src;
    if (q > 0xFFFF) return false;
    r_[AX] = lo16(q);
    r_[DX] = lo16(n % src);
  }
  return true;
}

// F6/F7: TEST (and its undocumented /1 alias), NOT, NEG, MUL, IMUL, DIV, IDIV.
void Cpu::group3(const Operand& o, bool word) {
  const uint32_t mask = mask_of(word);
  switch (o.reg) {
    case 0:
    case 1: {
      const uint32_t imm = word ? fetch16() : fetch8();
      alu(4, read_op(o, word), imm, word);
      break;
    }
    case 2: write_op(o, word, ~read_op(o, word) & mask); break;
    case 3: write_op(o, word, alu(5, 0, read_op(o, word), word)); break;
    case 4: {
      const uint32_t src = read_op(o, word);
      bool high;
      if (word) {
        const uint32_t p = static_cast<uint32_t>(r_[AX]) * src;
        r_[AX] = lo16(p);
        r_[DX] = lo16(p >> 16);
        high = r_[DX] != 0;
      } else {
        r_[AX] = lo16(static_cast<uint32_t>(reg8(0)) * src);
        high = (r_[AX] >> 8) != 0;
      }
      set_flag(kCF, high);
      set_flag(kOF, high);
      break;
    }
    case 5: {
      const uint32_t src = read_op(o, word);
      bool overflow;
      if (word) {
        const int32_t p = static_cast<int16_t>(r_[AX]) * static_cast<int16_t>(lo16(src));
        r_[AX] = lo16(static_cast<uint32_t>(p));
        r_[DX] = lo16(static_cast<uint32_t>(p) >> 16);
        overflow = p != static_cast<int16_t>(r_[AX]);
      } else {
        const int32_t p = static_cast<int8_t>(reg8(0)) * static_cast<int8_t>(lo8(src));
        r_[AX] = lo16(static_cast<uint32_t>(p));
        overflow = p != static_cast<int8_t>(lo8(r_[AX]));
      }
      set_flag(kCF, overflow);
      set_flag(kOF, overflow);
      break;
    }
    default:
      if (!divide(o, word, o.reg == 7)) interrupt(0);
      break;
  }
}

// ---- strings, I/O, interrupts ----------------------------------------------------------------

// A4-A7, AA-AF. With a REP prefix the whole repetition is one instruction. An external
// interrupt requested at its index is taken before it, which stacks the same IP (the
// prefix) and leaves the same final state as silicon suspending the string (D7).
void Cpu::string_op(uint8_t op) {
  const bool word = (op & 1) != 0;
  const uint32_t size = word ? 2 : 1;
  const uint16_t step = lo16(flag(kDF) ? 0u - size : size);
  const unsigned kind = (op >> 1) & 7;  // 2 MOVS, 3 CMPS, 5 STOS, 6 LODS, 7 SCAS
  const bool compares = kind == 3 || kind == 7;

  auto once = [&] {
    switch (kind) {
      case 2: {
        const uint32_t v = word ? rd16(data_seg(DS), r_[SI]) : rd8(data_seg(DS), r_[SI]);
        if (word) wr16(s_[ES], r_[DI], lo16(v)); else wr8(s_[ES], r_[DI], lo8(v));
        r_[SI] = lo16(r_[SI] + step);
        r_[DI] = lo16(r_[DI] + step);
        break;
      }
      case 3: {
        const uint32_t a = word ? rd16(data_seg(DS), r_[SI]) : rd8(data_seg(DS), r_[SI]);
        const uint32_t b = word ? rd16(s_[ES], r_[DI]) : rd8(s_[ES], r_[DI]);
        alu(7, a, b, word);
        r_[SI] = lo16(r_[SI] + step);
        r_[DI] = lo16(r_[DI] + step);
        break;
      }
      case 5:
        if (word) wr16(s_[ES], r_[DI], r_[AX]); else wr8(s_[ES], r_[DI], reg8(0));
        r_[DI] = lo16(r_[DI] + step);
        break;
      case 6:
        set_reg_op(AX, word, word ? rd16(data_seg(DS), r_[SI]) : rd8(data_seg(DS), r_[SI]));
        r_[SI] = lo16(r_[SI] + step);
        break;
      default: {
        const uint32_t b = word ? rd16(s_[ES], r_[DI]) : rd8(s_[ES], r_[DI]);
        alu(7, reg_op(AX, word), b, word);
        r_[DI] = lo16(r_[DI] + step);
        break;
      }
    }
  };

  if (rep_ == 0) {
    once();
    return;
  }
  while (r_[CX] != 0) {
    once();
    r_[CX] = lo16(r_[CX] - 1u);
    if (compares && flag(kZF) != (rep_ == 0xF3)) break;
  }
}

// A port missing from Program::io_inputs reads as an undriven bus: all ones.
uint16_t Cpu::port_in(uint16_t port, bool word) const {
  const auto it = program_.io_inputs.find(port);
  const uint16_t v = it == program_.io_inputs.end() ? 0xFFFF : it->second;
  return word ? v : static_cast<uint16_t>(v & 0xFF);
}

void Cpu::interrupt(uint8_t type) {
  out_.interrupts.push_back({type, out_.instructions});
  push(static_cast<uint16_t>(flags_ | kFlagsReadAsOne));
  set_flag(kIF, false);
  set_flag(kTF, false);
  push(s_[CS]);
  push(ip_);
  const uint16_t vec = static_cast<uint16_t>(type * 4u);
  ip_ = rd16(0, vec);
  s_[CS] = rd16(0, static_cast<uint16_t>(vec + 2u));
  interrupted_ = true;
}

void Cpu::take_external() {
  const auto due = [&](const Request& r) {
    return !r.taken && (r.arrived || r.req.instruction_index <= out_.instructions);
  };
  for (Request& r : requests_) {
    if (due(r) && r.req.nmi) {
      r.taken = true;
      interrupt(2);
      return;
    }
  }
  if (!flag(kIF)) return;
  for (Request& r : requests_) {
    if (due(r)) {
      r.taken = true;
      interrupt(r.req.type);
      return;
    }
  }
}

// A request that can be taken ends the halt; one not yet due arrives now.
bool Cpu::wake_from_halt() {
  const auto can_take = [&](const Request& r) { return !r.taken && (r.req.nmi || flag(kIF)); };
  for (const Request& r : requests_) {
    if (can_take(r) && (r.arrived || r.req.instruction_index <= out_.instructions)) return true;
  }
  for (Request& r : requests_) {
    if (can_take(r)) {
      r.arrived = true;
      return true;
    }
  }
  return false;
}

void Cpu::jump_rel(uint16_t disp) { ip_ = lo16(ip_ + disp); }

// ---- decode and execute ----------------------------------------------------------------------

void Cpu::execute(uint8_t op) {
  const bool word = (op & 1) != 0;

  if (op < 0x40 && (op & 7) < 6) {  // ALU r/m,reg / reg,r/m / acc,imm
    const unsigned alu_op = op >> 3;
    switch (op & 7) {
      case 0:
      case 1: {
        const Operand o = modrm();
        const uint32_t res = alu(alu_op, read_op(o, word), reg_op(o.reg, word), word);
        if (alu_op != 7) write_op(o, word, res);
        break;
      }
      case 2:
      case 3: {
        const Operand o = modrm();
        const uint32_t res = alu(alu_op, reg_op(o.reg, word), read_op(o, word), word);
        if (alu_op != 7) set_reg_op(o.reg, word, res);
        break;
      }
      default: {
        const uint32_t imm = word ? fetch16() : fetch8();
        const uint32_t res = alu(alu_op, reg_op(AX, word), imm, word);
        if (alu_op != 7) set_reg_op(AX, word, res);
        break;
      }
    }
    return;
  }

  if (op >= 0x40 && op < 0x60) {
    const unsigned n = op & 7;
    switch ((op >> 3) & 3) {
      case 0: r_[n] = lo16(inc_dec(r_[n], false, true)); break;
      case 1: r_[n] = lo16(inc_dec(r_[n], true, true)); break;
      case 2: push(n == SP ? lo16(r_[SP] - 2u) : r_[n]); break;  // 8086 pushes the new SP
      default: r_[n] = pop(); break;
    }
    return;
  }

  if (op >= 0x60 && op < 0x80) {  // Jcc; 60-6F are undocumented aliases of 70-7F on the 8086
    const uint16_t disp = sext8(fetch8());
    if (condition(op & 0xF)) jump_rel(disp);
    return;
  }

  if (op >= 0x91 && op <= 0x97) {
    std::swap(r_[AX], r_[op & 7]);
    return;
  }
  if (op >= 0xB0 && op <= 0xBF) {
    const bool w = op >= 0xB8;
    set_reg_op(op & 7, w, w ? fetch16() : fetch8());
    return;
  }
  if (op >= 0xD8 && op <= 0xDF) {  // ESC: no coprocessor, so only the operand fetch happens
    modrm();
    return;
  }

  switch (op) {
    case 0x06: case 0x0E: case 0x16: case 0x1E:
      push(s_[op >> 3]);
      return;
    case 0x07: case 0x0F: case 0x17: case 0x1F:  // 0F is POP CS on the 8086 (brief D11)
      s_[op >> 3] = pop();
      inhibit_trap_ = true;
      return;
    case 0x27:
    case 0x2F: {  // DAA / DAS
      const uint8_t al = reg8(0);
      const bool cf = flag(kCF);
      const bool sub = op == 0x2F;
      uint32_t v = al;
      set_flag(kCF, false);
      if ((al & 0xF) > 9 || flag(kAF)) {
        v = sub ? v - 6 : v + 6;
        set_flag(kCF, cf || v > 0xFF);
        set_flag(kAF, true);
      } else {
        set_flag(kAF, false);
      }
      if (al > 0x99 || cf) {
        v = sub ? v - 0x60 : v + 0x60;
        set_flag(kCF, true);
      }
      set_reg8(0, lo8(v));
      set_szp(lo8(v), false);
      return;
    }
    case 0x37:
    case 0x3F: {  // AAA / AAS: the 8086 adjusts AL alone, then carries into AH
      const bool adjust = (reg8(0) & 0xF) > 9 || flag(kAF);
      if (adjust) {
        const bool sub = op == 0x3F;
        set_reg8(0, lo8(sub ? reg8(0) - 6u : reg8(0) + 6u));
        set_reg8(4, lo8(sub ? reg8(4) - 1u : reg8(4) + 1u));
      }
      set_flag(kAF, adjust);
      set_flag(kCF, adjust);
      set_reg8(0, static_cast<uint8_t>(reg8(0) & 0xF));
      return;
    }
    case 0x80: case 0x81: case 0x82: case 0x83: {  // 82 is an undocumented alias of 80
      const bool w = op == 0x81 || op == 0x83;
      const Operand o = modrm();
      const uint32_t imm = op == 0x81 ? fetch16() : op == 0x83 ? sext8(fetch8()) : fetch8();
      const uint32_t res = alu(o.reg, read_op(o, w), imm, w);
      if (o.reg != 7) write_op(o, w, res);
      return;
    }
    case 0x84: case 0x85: {
      const Operand o = modrm();
      alu(4, read_op(o, word), reg_op(o.reg, word), word);
      return;
    }
    case 0x86: case 0x87: {
      const Operand o = modrm();
      const uint32_t a = read_op(o, word), b = reg_op(o.reg, word);
      write_op(o, word, b);
      set_reg_op(o.reg, word, a);
      return;
    }
    case 0x88: case 0x89: {
      const Operand o = modrm();
      write_op(o, word, reg_op(o.reg, word));
      return;
    }
    case 0x8A: case 0x8B: {
      const Operand o = modrm();
      set_reg_op(o.reg, word, read_op(o, word));
      return;
    }
    case 0x8C: {  // the 8086 decodes only the low two bits of the segment field
      const Operand o = modrm();
      write_op(o, true, s_[o.reg & 3]);
      return;
    }
    case 0x8D: {
      const Operand o = modrm();
      if (!o.is_mem) throw Unimplemented{};
      r_[o.reg] = o.off;
      return;
    }
    case 0x8E: {  // MOV CS,r/m is legal on the 8086 and acts as a jump
      const Operand o = modrm();
      s_[o.reg & 3] = lo16(read_op(o, true));
      inhibit_trap_ = true;
      return;
    }
    case 0x8F: {
      const Operand o = modrm();
      if (o.reg != 0) throw Unimplemented{};
      write_op(o, true, pop());
      return;
    }
    case 0x90: return;
    case 0x98: r_[AX] = sext8(reg8(0)); return;
    case 0x99: r_[DX] = (r_[AX] & 0x8000) ? 0xFFFF : 0; return;
    case 0x9A: {
      const uint16_t off = fetch16(), seg = fetch16();
      push(s_[CS]);
      push(ip_);
      s_[CS] = seg;
      ip_ = off;
      return;
    }
    case 0x9B: throw Unimplemented{};  // WAIT: TEST# is not part of a golden Program
    case 0x9C: push(static_cast<uint16_t>(flags_ | kFlagsReadAsOne)); return;
    case 0x9D: flags_ = static_cast<uint16_t>(pop() & kFlagsMask); return;
    case 0x9E:
      flags_ = static_cast<uint16_t>((flags_ & 0xFF00u) | (reg8(4) & (kFlagsMask & 0xFFu)));
      return;
    case 0x9F: set_reg8(4, lo8((flags_ & kFlagsMask) | (kFlagsReadAsOne & 0xFFu))); return;
    case 0xA0: case 0xA1: {
      const uint16_t off = fetch16();
      set_reg_op(AX, word, word ? rd16(data_seg(DS), off) : rd8(data_seg(DS), off));
      return;
    }
    case 0xA2: case 0xA3: {
      const uint16_t off = fetch16();
      if (word) wr16(data_seg(DS), off, r_[AX]); else wr8(data_seg(DS), off, reg8(0));
      return;
    }
    case 0xA4: case 0xA5: case 0xA6: case 0xA7:
    case 0xAA: case 0xAB: case 0xAC: case 0xAD: case 0xAE: case 0xAF:
      string_op(op);
      return;
    case 0xA8: case 0xA9:
      alu(4, reg_op(AX, word), word ? fetch16() : fetch8(), word);
      return;
    case 0xC0: case 0xC1: case 0xC2: case 0xC3: {  // C0/C1 alias C2/C3 on the 8086
      const uint16_t pop_bytes = (op & 1) ? uint16_t{0} : fetch16();
      ip_ = pop();
      r_[SP] = lo16(r_[SP] + pop_bytes);
      return;
    }
    case 0xC4: case 0xC5: {
      const Operand o = modrm();
      if (!o.is_mem) throw Unimplemented{};
      r_[o.reg] = rd16(o.seg, o.off);
      s_[op == 0xC4 ? ES : DS] = rd16(o.seg, lo16(o.off + 2u));
      return;
    }
    case 0xC6: case 0xC7: {
      const Operand o = modrm();
      if (o.reg != 0) throw Unimplemented{};
      write_op(o, word, word ? fetch16() : fetch8());
      return;
    }
    case 0xC8: case 0xC9: case 0xCA: case 0xCB: {  // C8/C9 alias CA/CB on the 8086
      const uint16_t pop_bytes = (op & 1) ? uint16_t{0} : fetch16();
      ip_ = pop();
      s_[CS] = pop();
      r_[SP] = lo16(r_[SP] + pop_bytes);
      return;
    }
    case 0xCC: interrupt(3); return;
    case 0xCD: interrupt(fetch8()); return;
    case 0xCE: if (flag(kOF)) interrupt(4); return;
    case 0xCF:
      ip_ = pop();
      s_[CS] = pop();
      flags_ = static_cast<uint16_t>(pop() & kFlagsMask);
      return;
    case 0xD0: case 0xD1: case 0xD2: case 0xD3: {
      const Operand o = modrm();
      const unsigned count = op >= 0xD2 ? reg8(1) : 1;
      write_op(o, word, shift(o.reg, read_op(o, word), count, word));
      return;
    }
    case 0xD4: {  // AAM: a zero base raises the divide error
      const uint8_t base = fetch8();
      if (base == 0) {
        interrupt(0);
        return;
      }
      const uint8_t al = reg8(0);
      set_reg8(4, static_cast<uint8_t>(al / base));
      set_reg8(0, static_cast<uint8_t>(al % base));
      set_szp(reg8(0), false);
      return;
    }
    case 0xD5: {
      const uint8_t base = fetch8();
      set_reg8(0, lo8(reg8(4) * static_cast<uint32_t>(base) + reg8(0)));
      set_reg8(4, 0);
      set_szp(reg8(0), false);
      return;
    }
    case 0xD6: set_reg8(0, flag(kCF) ? 0xFF : 0x00); return;  // undocumented SALC (brief D11)
    case 0xD7: set_reg8(0, rd8(data_seg(DS), lo16(r_[BX] + reg8(0)))); return;
    case 0xE0: case 0xE1: case 0xE2: {  // LOOPNE / LOOPE / LOOP
      const uint16_t disp = sext8(fetch8());
      r_[CX] = lo16(r_[CX] - 1u);
      const bool z_ok = op == 0xE2 || (flag(kZF) == (op == 0xE1));
      if (r_[CX] != 0 && z_ok) jump_rel(disp);
      return;
    }
    case 0xE3: {
      const uint16_t disp = sext8(fetch8());
      if (r_[CX] == 0) jump_rel(disp);
      return;
    }
    case 0xE4: case 0xE5: case 0xEC: case 0xED: {
      const uint16_t port = op >= 0xEC ? r_[DX] : fetch8();
      set_reg_op(AX, word, port_in(port, word));
      return;
    }
    case 0xE6: case 0xE7: case 0xEE: case 0xEF: {
      const uint16_t port = op >= 0xEE ? r_[DX] : fetch8();
      out_.io_writes.push_back({port, word ? r_[AX] : static_cast<uint16_t>(reg8(0)), word});
      return;
    }
    case 0xE8: {
      const uint16_t disp = fetch16();
      push(ip_);
      jump_rel(disp);
      return;
    }
    case 0xE9: jump_rel(fetch16()); return;
    case 0xEA: {
      const uint16_t off = fetch16();
      s_[CS] = fetch16();
      ip_ = off;
      return;
    }
    case 0xEB: jump_rel(sext8(fetch8())); return;
    case 0xF5: set_flag(kCF, !flag(kCF)); return;
    case 0xF6: case 0xF7: group3(modrm(), word); return;
    case 0xF8: case 0xF9: set_flag(kCF, op == 0xF9); return;
    case 0xFA: set_flag(kIF, false); return;
    case 0xFB:
      set_flag(kIF, true);
      shadow_ = true;
      return;
    case 0xFC: case 0xFD: set_flag(kDF, op == 0xFD); return;
    case 0xFE: {
      const Operand o = modrm();
      if (o.reg > 1) throw Unimplemented{};
      write_op(o, false, inc_dec(read_op(o, false), o.reg == 1, false));
      return;
    }
    case 0xFF: {
      const Operand o = modrm();
      switch (o.reg) {
        case 0:
        case 1: write_op(o, true, inc_dec(read_op(o, true), o.reg == 1, true)); return;
        case 2: {
          const uint16_t target = lo16(read_op(o, true));
          push(ip_);
          ip_ = target;
          return;
        }
        case 3:
        case 5: {
          if (!o.is_mem) throw Unimplemented{};
          const uint16_t off = rd16(o.seg, o.off), seg = rd16(o.seg, lo16(o.off + 2u));
          if (o.reg == 3) {
            push(s_[CS]);
            push(ip_);
          }
          s_[CS] = seg;
          ip_ = off;
          return;
        }
        case 4: ip_ = lo16(read_op(o, true)); return;
        case 6:
          push(!o.is_mem && o.rm == SP ? lo16(r_[SP] - 2u) : lo16(read_op(o, true)));
          return;
        default: throw Unimplemented{};
      }
    }
    default: throw Unimplemented{};
  }
}

}  // namespace i8086::golden::detail
