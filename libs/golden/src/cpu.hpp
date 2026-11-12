#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "i8086/golden/emulator.hpp"

namespace i8086::golden::detail {

inline constexpr uint16_t kCF = 0x0001, kPF = 0x0004, kAF = 0x0010, kZF = 0x0040, kSF = 0x0080,
                          kTF = 0x0100, kIF = 0x0200, kDF = 0x0400, kOF = 0x0800;
// The nine architectural flags. Bits 15-12 and bit 1 are not storage on the 8086; they read as 1
// whenever FLAGS reaches memory or AH (kFlagsReadAsOne).
inline constexpr uint16_t kFlagsMask = 0x0FD5;
inline constexpr uint16_t kFlagsReadAsOne = 0xF002;

// One run of the golden emulator: 1 MB of RAM, the register file, and the Result being recorded.
class Cpu {
 public:
  Cpu(const Program& program, Result& out);
  void run(uint64_t max_instructions);

 private:
  enum class Step { Continue, Halt };
  enum Reg16 { AX, CX, DX, BX, SP, BP, SI, DI };
  enum Seg { ES, CS, SS, DS };

  struct Operand {
    bool is_mem = false;
    uint8_t reg = 0;  // ModRM reg field
    uint8_t rm = 0;   // register number when !is_mem
    uint16_t seg = 0, off = 0;
  };

  Step step();
  void execute(uint8_t op);

  uint8_t fetch8();
  uint16_t fetch16();

  uint8_t rd8(uint16_t seg, uint16_t off) const;
  uint16_t rd16(uint16_t seg, uint16_t off) const;
  void wr8(uint16_t seg, uint16_t off, uint8_t v);
  void wr16(uint16_t seg, uint16_t off, uint16_t v);
  void push(uint16_t v);
  uint16_t pop();

  uint8_t reg8(unsigned n) const;
  void set_reg8(unsigned n, uint8_t v);
  uint16_t data_seg(Seg dflt) const;

  Operand modrm();
  uint32_t read_op(const Operand&, bool word) const;
  void write_op(const Operand&, bool word, uint32_t v);
  uint32_t reg_op(unsigned n, bool word) const;
  void set_reg_op(unsigned n, bool word, uint32_t v);

  bool flag(uint16_t f) const { return (flags_ & f) != 0; }
  void set_flag(uint16_t f, bool on);
  void set_szp(uint32_t res, bool word);
  uint32_t alu(unsigned op, uint32_t a, uint32_t b, bool word);
  uint32_t inc_dec(uint32_t v, bool dec, bool word);
  uint32_t shift(unsigned op, uint32_t v, unsigned count, bool word);
  bool condition(unsigned cc) const;
  void group3(const Operand&, bool word);
  bool divide(const Operand&, bool word, bool is_signed);
  void string_op(uint8_t op);
  uint16_t port_in(uint16_t port, bool word) const;

  void interrupt(uint8_t type);
  void jump_rel(uint16_t disp);

  std::vector<uint8_t> mem_;
  std::array<uint16_t, 8> r_{};
  std::array<uint16_t, 4> s_{};
  uint16_t ip_ = 0, flags_ = 0;

  const Program& program_;
  Result& out_;

  // Per-instruction decode state.
  uint16_t start_ip_ = 0;
  int seg_override_ = -1;
  uint8_t rep_ = 0;  // 0, 0xF2 (REPNE) or 0xF3 (REP/REPE)
  bool interrupted_ = false;
  bool inhibit_trap_ = false;
};

}  // namespace i8086::golden::detail
