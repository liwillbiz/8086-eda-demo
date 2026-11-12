// Model-vs-golden differential fuzz (design doc §8, M5): random instruction streams from
// the v1 subset (design doc §7.3) run on the golden emulator and on the device model through
// SimTester. After every instruction the program reports AX BX CX DX SI DI BP and FLAGS;
// the generator tracks which flags are undefined at each report and the diff masks them.

#include <gtest/gtest.h>

#include <format>
#include <random>

#include "functional.hpp"
#include "i8086/golden/rom.hpp"
#include "i8086/model/device_model.hpp"
#include "i8086/tester/sim_tester.hpp"

namespace i8086::testprog {
namespace {

constexpr uint32_t kSeed = 8086;
constexpr int kPrograms = 64;
constexpr int kInstructions = 40;
constexpr std::size_t kRomSize = 0x1000;  // same layout as testprog/roms/inc/i86rom.inc
constexpr uint16_t kRomSeg = 0xFF00;
constexpr uint16_t kDataSeg = 0x0200;
constexpr uint16_t kDataWords = 16;       // DS:0000h-001Fh, plus one spare byte for odd words

class Generator {
 public:
  explicit Generator(uint32_t seed) : rng_(seed) {}

  std::vector<uint8_t> program(std::vector<uint16_t>& masks, std::string& listing) {
    code_.clear();
    masks.clear();
    listing.clear();
    emit({0x31, 0xC0});                             // xor ax,ax
    emit({0x8E, 0xD0});                             // mov ss,ax
    emit({0xBC, 0x00, 0x10});                       // mov sp,1000h
    emit({0xB8, lo(kDataSeg), hi(kDataSeg)});       // mov ax,DATA_SEG
    emit({0x8E, 0xD8});                             // mov ds,ax
    for (uint16_t a = 0; a < kDataWords * 2 + 2; a += 2) {
      const uint16_t v = word();
      emit({0xC7, 0x06, lo(a), hi(a), lo(v), hi(v)});  // mov word [a],v
    }
    for (int r : {0, 1, 2, 3, 5, 6, 7}) {
      const uint16_t v = word();
      emit({static_cast<uint8_t>(0xB8 + r), lo(v), hi(v)});  // mov r16,v
    }
    undefined_ = flag::AF;  // XOR AX,AX above left AF undefined
    for (int i = 0; i < kInstructions; ++i) {
      listing += instruction() + "; ";
      report();
      masks.push_back(undefined_);
    }
    for (uint16_t a = 0; a < kDataWords * 2; a += 2) {
      emit({0xA1, lo(a), hi(a)});  // mov ax,[a]
      emit({0xE7, static_cast<uint8_t>(kPortData)});
    }
    emit({0xF4});  // hlt

    std::vector<uint8_t> rom(kRomSize, 0xF4);
    if (code_.size() > kRomSize - 16) throw std::length_error("fuzz program too long");
    std::ranges::copy(code_, rom.begin());
    const uint8_t jmp[] = {0xEA, 0x00, 0x00, lo(kRomSeg), hi(kRomSeg)};  // jmp ROM_SEG:0
    std::ranges::copy(jmp, rom.begin() + (kRomSize - 16));
    return rom;
  }

 private:
  static uint8_t lo(uint16_t v) { return static_cast<uint8_t>(v & 0xFF); }
  static uint8_t hi(uint16_t v) { return static_cast<uint8_t>(v >> 8); }
  void emit(std::initializer_list<uint8_t> b) { code_.insert(code_.end(), b); }
  int pick(int n) { return std::uniform_int_distribution<int>(0, n - 1)(rng_); }
  uint16_t word() {
    // Bias toward edge values so carries, overflows and zero results are common.
    static constexpr uint16_t kEdges[] = {0x0000, 0x0001, 0x007F, 0x0080, 0x00FF, 0x7FFF, 0x8000, 0xFFFF};
    return pick(3) == 0 ? kEdges[pick(8)] : static_cast<uint16_t>(pick(0x10000));
  }
  int reg16() {
    static constexpr int kRegs[] = {0, 1, 2, 3, 5, 6, 7};  // never SP
    return kRegs[pick(7)];
  }
  uint16_t mem_addr() { return static_cast<uint16_t>(pick(kDataWords * 2)); }

  // ModR/M for a register operand, or a direct [disp16] memory operand (data area only).
  std::string rm(bool wide, int reg_field, std::vector<uint8_t>& out) {
    if (pick(3) == 0) {
      const uint16_t a = mem_addr();
      out.push_back(static_cast<uint8_t>(0x06 | reg_field << 3));
      out.push_back(lo(a));
      out.push_back(hi(a));
      return std::format("[{:04X}h]", a);
    }
    const int r = wide ? reg16() : pick(8);
    out.push_back(static_cast<uint8_t>(0xC0 | reg_field << 3 | r));
    return std::format("{}{}", wide ? "r16." : "r8.", r);
  }

  // ADD OR ADC SBB AND SUB XOR CMP (ALU group index).
  void alu_flags(int op) {
    undefined_ = (op == 1 || op == 4 || op == 6) ? flag::AF : 0;
  }

  std::string instruction() {
    static constexpr const char* kAlu[] = {"add", "or", "adc", "sbb", "and", "sub", "xor", "cmp"};
    std::vector<uint8_t> b;
    std::string text;
    const bool wide = pick(2) == 1;
    switch (pick(9)) {
      case 0: case 1: {  // ALU r/m,reg or reg,r/m
        const int op = pick(8);
        const bool to_reg = pick(2) == 1;
        const int reg = wide ? reg16() : pick(8);
        b.push_back(static_cast<uint8_t>(op * 8 + (to_reg ? 2 : 0) + (wide ? 1 : 0)));
        text = std::format("{} {} ({})", kAlu[op], rm(wide, reg, b), to_reg ? "to reg" : "to r/m");
        alu_flags(op);
        break;
      }
      case 2: {  // ALU r/m,imm (80/81/83)
        const int op = pick(8);
        const int form = wide ? (pick(2) ? 0x81 : 0x83) : 0x80;
        b.push_back(static_cast<uint8_t>(form));
        text = std::format("{} {},imm", kAlu[op], rm(wide, op, b));
        const uint16_t v = word();
        b.push_back(lo(v));
        if (form == 0x81) b.push_back(hi(v));
        alu_flags(op);
        break;
      }
      case 3: {  // INC/DEC: CF unchanged
        const bool dec = pick(2) == 1;
        if (wide && pick(2)) {
          b.push_back(static_cast<uint8_t>((dec ? 0x48 : 0x40) + reg16()));
          text = dec ? "dec r16" : "inc r16";
        } else {
          b.push_back(wide ? 0xFF : 0xFE);
          text = std::string(dec ? "dec " : "inc ") + rm(wide, dec ? 1 : 0, b);
        }
        undefined_ &= flag::CF;
        break;
      }
      case 4: {  // SHL/SHR r/m,CL with a known count
        int count = pick(4) == 0 ? pick(2) : pick(20);
        const bool right = pick(2) == 1;
        b.push_back(wide ? 0xD3 : 0xD2);
        const std::string operand = rm(wide, right ? 5 : 4, b);
        // Open (docs/decisions.md, M5): with CL = 0 the golden emulator writes a memory
        // operand back unchanged and the model runs no write cycle. Registers only.
        if (count == 0 && operand.front() == '[') count = 1;
        emit({0xB1, static_cast<uint8_t>(count)});  // mov cl,count
        text = std::format("mov cl,{}; {} {},cl", count, right ? "shr" : "shl", operand);
        if (count == 1) undefined_ = flag::AF;
        else if (count > 1) undefined_ = flag::AF | flag::OF;
        break;
      }
      case 5: {  // MOV r/m,reg / reg,r/m
        const bool to_reg = pick(2) == 1;
        const int reg = wide ? reg16() : pick(8);
        b.push_back(static_cast<uint8_t>(0x88 + (to_reg ? 2 : 0) + (wide ? 1 : 0)));
        text = "mov " + rm(wide, reg, b);
        break;
      }
      case 6: {  // MOV reg,imm
        const uint16_t v = word();
        if (wide) {
          b = {static_cast<uint8_t>(0xB8 + reg16()), lo(v), hi(v)};
        } else {
          b = {static_cast<uint8_t>(0xB0 + pick(8)), lo(v)};
        }
        text = "mov reg,imm";
        break;
      }
      case 7: {  // PUSH r16; POP r16
        b = {static_cast<uint8_t>(0x50 + reg16()), static_cast<uint8_t>(0x58 + reg16())};
        text = "push/pop";
        break;
      }
      default: {  // CLC / STC
        const bool set = pick(2) == 1;
        b = {static_cast<uint8_t>(set ? 0xF9 : 0xF8)};
        text = set ? "stc" : "clc";
        undefined_ &= ~flag::CF;
        break;
      }
    }
    code_.insert(code_.end(), b.begin(), b.end());
    return text;
  }

  // push ax; out AX..BP; pushf; pop ax; out FLAGS; pop ax
  void report() {
    const uint8_t data = static_cast<uint8_t>(kPortData);
    emit({0x50, 0xE7, data});
    for (const uint8_t mov : std::initializer_list<uint8_t>{0xD8, 0xC8, 0xD0, 0xF0, 0xF8, 0xE8}) emit({0x89, mov, 0xE7, data});  // mov ax,r
    emit({0x9C, 0x58, 0xE7, static_cast<uint8_t>(kPortFlags), 0x58});
  }

  std::mt19937 rng_;
  std::vector<uint8_t> code_;
  uint16_t undefined_ = 0;
};

TEST(DifferentialFuzz, ModelMatchesGoldenOnRandomStreams) {
  const spec::Spec spec = spec::Spec::load(I86_SOURCE_DIR "/spec/spec.yaml");
  const model::DieProfile die = model::DieProfile::load(I86_SOURCE_DIR "/spec/dies/good.yaml");
  const golden::Emulator golden;
  Generator gen(kSeed);

  int failures = 0;
  std::size_t instructions = 0, reports = 0;
  for (int p = 0; p < kPrograms; ++p) {
    std::vector<uint16_t> masks;
    std::string listing;
    const auto rom = gen.program(masks, listing);

    const golden::Result g = golden.run(golden::rom_program(rom));
    ASSERT_EQ(g.stop, golden::StopReason::Halt) << "program " << p << ": " << listing;

    tester::SimTester t(spec, model::make_behavioral(spec, die, {}), tester::ElectricalModel(die));
    tester::TimingSet ts;
    ts.period_ns = 200;
    t.set_timing(0, ts);
    t.power(true);
    tester::MemoryImage image;
    image.load(kRomTop - static_cast<uint32_t>(rom.size()), rom);
    tester::RunOpts opts;
    opts.max_cycles = 200'000;
    const auto r = t.run_program(image, opts);

    DiffOptions o;
    o.per_report = masks;
    o.max_messages = 3;
    const auto expected = observe(g);
    const auto d = diff(expected, observe(t.last_trace(), r.pass), o);
    instructions += kInstructions;
    reports += expected.flags().size();
    if (!d.empty()) {
      ++failures;
      std::string msg;
      for (const auto& m : d) msg += "\n    " + m;
      ADD_FAILURE() << "program " << p << " (seed " << kSeed << "):" << msg << "\n  stream: " << listing;
      if (failures >= 5) break;
    }
  }
  EXPECT_EQ(reports, instructions);  // every instruction reported its FLAGS
  RecordProperty("instructions", static_cast<int>(instructions));
}

}  // namespace
}  // namespace i8086::testprog
