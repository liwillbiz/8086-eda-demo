#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include "i8086/golden/emulator.hpp"
#include "i8086/golden/rom.hpp"
#include "test_util.hpp"

namespace i8086::golden {

static bool operator==(const MemWrite& a, const MemWrite& b) {
  return a.addr == b.addr && a.value == b.value;
}

namespace {

using namespace test;

// Interrupt vector `type` -> 0000:handler, with `code` placed at the handler.
void add_handler(Program& p, uint8_t type, uint16_t handler, std::vector<uint8_t> code) {
  const auto lo = static_cast<uint8_t>(handler), hi = static_cast<uint8_t>(handler >> 8);
  p.segments.push_back({type * 4u, {lo, hi, 0, 0}});
  p.segments.push_back({handler, std::move(code)});
}

// ---- reset, stop reasons, ROM loading --------------------------------------------------------

TEST(Golden, ResetState) {
  const Registers r = reset_registers();
  EXPECT_EQ(r.cs, 0xFFFF);
  EXPECT_EQ(r.ip, 0x0000);
  EXPECT_EQ(r.flags, 0x0000);
}

TEST(Golden, FirstFetchIsFFFF0) {
  Program p;
  p.segments.push_back({0xFFFF0, {0xF4}});
  const Result r = run(p);
  EXPECT_EQ(r.stop, StopReason::Halt);
  EXPECT_EQ(r.instructions, 1u);
  EXPECT_EQ(r.regs.cs, 0xFFFF);
  EXPECT_EQ(r.regs.ip, 0x0001);  // IP is left past the HLT
  EXPECT_EQ(r.regs.ds, 0);
  EXPECT_EQ(r.regs.ss, 0);
  EXPECT_EQ(r.regs.es, 0);
  EXPECT_EQ(r.regs.flags, 0);
}

TEST(Golden, InstructionLimit) {
  const Result r = run(boot({0xEB, 0xFE}), 10);  // JMP $
  EXPECT_EQ(r.stop, StopReason::InstructionLimit);
  EXPECT_EQ(r.instructions, 10u);
  EXPECT_EQ(r.regs.ip, 0x0100);

  const Result none = run(boot({}), 0);
  EXPECT_EQ(none.stop, StopReason::InstructionLimit);
  EXPECT_EQ(none.instructions, 0u);
  EXPECT_EQ(none.regs.cs, 0xFFFF);
}

TEST(Golden, UnimplementedStopsAtInstructionStart) {
  const Result wait = run_code({0xB0, 0x01, 0x2E, 0x9B});  // MOV AL,1; CS: WAIT
  EXPECT_EQ(wait.stop, StopReason::Unimplemented);
  EXPECT_EQ(wait.instructions, 2u);
  EXPECT_EQ(wait.regs.ip, 0x0102);  // at the prefix, not the opcode
  EXPECT_EQ(wait.regs.ax, 0x0001);

  EXPECT_EQ(run_code({0xFE, 0xF8}).stop, StopReason::Unimplemented);  // FE /7
  EXPECT_EQ(run_code({0x8D, 0xC0}).stop, StopReason::Unimplemented);  // LEA AX,AX
}

TEST(Golden, RomProgramEndsAtTopOfMemory) {
  std::vector<uint8_t> rom(16, 0x90);
  rom[0] = 0xF4;
  const Program p = rom_program(rom, {{0x10, 0x55}});
  ASSERT_EQ(p.segments.size(), 1u);
  EXPECT_EQ(p.segments[0].first, 0xFFFF0u);
  EXPECT_EQ(p.io_inputs.at(0x10), 0x55);
  const Result r = run(p);
  EXPECT_EQ(r.stop, StopReason::Halt);
  EXPECT_EQ(r.instructions, 1u);

  EXPECT_EQ(rom_program(std::vector<uint8_t>(0x10000, 0)).segments[0].first, 0xF0000u);
  EXPECT_THROW(rom_program({}), std::invalid_argument);
  EXPECT_THROW(rom_program(std::vector<uint8_t>(kMemoryTop + 1, 0)), std::invalid_argument);
}

TEST(Golden, LoadRomFromFile) {
  const auto path = std::filesystem::temp_directory_path() / "i86golden_rom_test.bin";
  {
    std::ofstream f(path, std::ios::binary);
    const char bytes[] = {'\xB0', '\x2A', '\xF4'};  // MOV AL,42; HLT
    f.write(bytes, sizeof bytes);
  }
  EXPECT_EQ(read_binary(path), (std::vector<uint8_t>{0xB0, 0x2A, 0xF4}));
  // A 3-byte ROM sits at FFFFDh, so the reset fetch at FFFF0h reads zeros; check placement only.
  EXPECT_EQ(load_rom(path).segments[0].first, 0xFFFFDu);
  std::filesystem::remove(path);
  EXPECT_THROW(read_binary(path), std::runtime_error);
}

// ---- brief §7 D-tests ------------------------------------------------------------------------

TEST(Golden, D1_AddFlags) {
  const Result r = run_code({0xB0, 0x7F, 0x04, 0x01});  // MOV AL,7Fh; ADD AL,1
  EXPECT_EQ(r.regs.ax & 0xFF, 0x80);
  EXPECT_EQ(r.regs.flags & kArith, OF | SF | AF);  // ZF=0, PF=0, CF=0
}

TEST(Golden, D2_IncDecPreserveCF) {
  const Result inc = run_code({0xB8, 0xFF, 0xFF, 0xF9, 0x40});  // MOV AX,FFFFh; STC; INC AX
  EXPECT_EQ(inc.regs.ax, 0x0000);
  EXPECT_EQ(inc.regs.flags & kArith, ZF | AF | PF | CF);

  const Result dec = run_code({0xF8, 0x48});  // CLC; DEC AX (AX=0)
  EXPECT_EQ(dec.regs.ax, 0xFFFF);
  EXPECT_EQ(dec.regs.flags & kArith, SF | AF | PF);

  const Result inc8 = run_code({0xB0, 0x7F, 0xF9, 0xFE, 0xC0});  // MOV AL,7Fh; STC; INC AL
  EXPECT_EQ(inc8.regs.ax, 0x0080);
  EXPECT_EQ(inc8.regs.flags & kArith, OF | SF | AF | CF);
}

TEST(Golden, D3_DivideErrorStacksIpPastDiv) {
  // MOV SP,1000h; MOV AX,1; MOV BL,0; DIV BL   (DIV at 0108h, next instruction at 010Ah)
  Program p = boot({0xBC, 0x00, 0x10, 0xB8, 0x01, 0x00, 0xB3, 0x00, 0xF6, 0xF3});
  add_handler(p, 0, 0x0200, {0x58, 0x5B, 0x59, 0xF4});  // POP AX (IP); POP BX (CS); POP CX (FLAGS)
  const Result r = run(p);
  EXPECT_EQ(r.stop, StopReason::Halt);
  ASSERT_EQ(r.interrupts.size(), 1u);
  EXPECT_EQ(r.interrupts[0].type, 0);
  EXPECT_EQ(r.interrupts[0].instruction_index, 4u);
  EXPECT_EQ(r.regs.ax, 0x010A);
  EXPECT_EQ(r.regs.bx, 0x0000);
  EXPECT_EQ(r.regs.cx & 0xF002, 0xF002);
}

// ---- external interrupt requests (M8) -------------------------------------------------------

TEST(Golden, IntrIsTakenAfterTheStiShadow) {
  // MOV SP,1000h; STI; NOP; NOP   (second NOP, index 4, at 0105h)
  Program p = boot({0xBC, 0x00, 0x10, 0xFB, 0x90, 0x90});
  add_handler(p, 0x20, 0x0200, {0x58, 0xF4});  // POP AX (IP); HLT
  p.interrupts.push_back({.instruction_index = 3, .nmi = false, .type = 0x20});
  const Result r = run(p);
  ASSERT_EQ(r.interrupts.size(), 1u);
  EXPECT_EQ(r.interrupts[0].type, 0x20);
  EXPECT_EQ(r.interrupts[0].instruction_index, 4u);
  EXPECT_EQ(r.regs.ax, 0x0105);
}

TEST(Golden, MaskedIntrWaitsForSti) {
  // MOV SP,1000h; NOP; STI; NOP; HLT at 0106h
  Program p = boot({0xBC, 0x00, 0x10, 0x90, 0xFB, 0x90});
  add_handler(p, 0x20, 0x0200, {0x58, 0xF4});
  p.interrupts.push_back({.instruction_index = 1, .nmi = false, .type = 0x20});
  const Result r = run(p);
  ASSERT_EQ(r.interrupts.size(), 1u);
  EXPECT_EQ(r.interrupts[0].instruction_index, 5u);
  EXPECT_EQ(r.regs.ax, 0x0106);
}

TEST(Golden, NmiDuringHaltResumesAfterHlt) {
  // MOV SP,1000h; HLT; INC BX; (HLT)
  Program p = boot({0xBC, 0x00, 0x10, 0xF4, 0x43});
  add_handler(p, 2, 0x0200, {0x41, 0xCF});  // INC CX; IRET
  p.interrupts.push_back({.instruction_index = InterruptRequest::kDuringHalt, .nmi = true});
  const Result r = run(p);
  EXPECT_EQ(r.stop, StopReason::Halt);
  ASSERT_EQ(r.interrupts.size(), 1u);
  EXPECT_EQ(r.interrupts[0].type, 2);
  EXPECT_EQ(r.regs.cx, 1);
  EXPECT_EQ(r.regs.bx, 1);
}

TEST(Golden, MaskedIntrDoesNotWakeHalt) {
  Program p = boot({0xBC, 0x00, 0x10, 0xFA, 0xF4, 0x43});  // CLI; HLT; INC BX
  p.interrupts.push_back({.instruction_index = InterruptRequest::kDuringHalt, .nmi = false, .type = 0x20});
  const Result r = run(p);
  EXPECT_EQ(r.stop, StopReason::Halt);
  EXPECT_TRUE(r.interrupts.empty());
  EXPECT_EQ(r.regs.bx, 0);
}

TEST(Golden, D4_SegmentDefaults) {
  Program p = boot({
      0xB8, 0x00, 0x01, 0x8E, 0xD0,  // MOV AX,0100h; MOV SS,AX
      0xB8, 0x00, 0x02, 0x8E, 0xD8,  // MOV AX,0200h; MOV DS,AX
      0x31, 0xDB, 0x31, 0xED,        // XOR BX,BX; XOR BP,BP
      0x8B, 0x46, 0x02,              // MOV AX,[BP+2]     -> SS
      0x8B, 0x4F, 0x02,              // MOV CX,[BX+2]     -> DS
      0x3E, 0x8B, 0x56, 0x02,        // MOV DX,DS:[BP+2]  -> DS
  });
  p.segments.push_back({0x1002, {0x34, 0x12}});
  p.segments.push_back({0x2002, {0x78, 0x56}});
  const Result r = run(p);
  EXPECT_EQ(r.regs.ax, 0x1234);
  EXPECT_EQ(r.regs.cx, 0x5678);
  EXPECT_EQ(r.regs.dx, 0x5678);
}

TEST(Golden, D5_SegmentWrapsAt1MB) {
  // MOV AX,FFFFh; MOV ES,AX; MOV BYTE ES:[0010h],AAh
  const Result r = run_code({0xB8, 0xFF, 0xFF, 0x8E, 0xC0, 0x26, 0xC6, 0x06, 0x10, 0x00, 0xAA});
  ASSERT_EQ(r.mem_writes.size(), 1u);
  EXPECT_EQ(r.mem_writes[0], (MemWrite{0x00000, 0xAA}));
}

TEST(Golden, D6_RepMovswDown) {
  // MOV SI,0306h; MOV DI,0406h; MOV CX,4; STD; REP MOVSW
  Program p = boot({0xBE, 0x06, 0x03, 0xBF, 0x06, 0x04, 0xB9, 0x04, 0x00, 0xFD, 0xF3, 0xA5});
  p.segments.push_back({0x300, {1, 2, 3, 4, 5, 6, 7, 8}});
  const Result r = run(p);
  EXPECT_EQ(r.regs.cx, 0);
  EXPECT_EQ(r.regs.si, 0x02FE);
  EXPECT_EQ(r.regs.di, 0x03FE);
  EXPECT_TRUE(r.regs.flags & DF);
  const std::vector<MemWrite> expected{{0x406, 7}, {0x407, 8}, {0x404, 5}, {0x405, 6},
                                       {0x402, 3}, {0x403, 4}, {0x400, 1}, {0x401, 2}};
  EXPECT_EQ(r.mem_writes, expected);
  EXPECT_EQ(r.instructions, 7u);  // the whole REP MOVSW is one instruction
}

TEST(Golden, D8_ShiftCountNotMasked) {
  // MOV AX,1; MOV CL,33; SHL AX,CL. A 286 masks the count to 1 and gets AX=2.
  const Result r = run_code({0xB8, 0x01, 0x00, 0xB1, 0x21, 0xD3, 0xE0});
  EXPECT_EQ(r.regs.ax, 0x0000);
  EXPECT_EQ(r.regs.flags & (CF | SF | ZF | PF), ZF | PF);
}

TEST(Golden, D9_PushfReadsHighBitsAsOne) {
  // XOR AX,AX; PUSH AX; POPF; PUSHF; POP AX
  const Result r = run_code({0x31, 0xC0, 0x50, 0x9D, 0x9C, 0x58});
  EXPECT_EQ(r.regs.ax, 0xF002);
  EXPECT_EQ(r.regs.flags, 0x0000);  // Result::regs.flags holds only the nine real flags
}

TEST(Golden, D10_SingleStep) {
  // MOV AX,0100h; PUSH AX; POPF (TF=1); NOP; NOP; NOP; HLT
  Program p = boot({0xB8, 0x00, 0x01, 0x50, 0x9D, 0x90, 0x90, 0x90});
  add_handler(p, 1, 0x0400, {0x9C, 0x5F, 0x46, 0xCF});  // PUSHF; POP DI; INC SI; IRET
  const Result r = run(p);
  EXPECT_EQ(r.stop, StopReason::Halt);
  ASSERT_EQ(r.interrupts.size(), 3u);
  for (size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(r.interrupts[i].type, 1);
    EXPECT_EQ(r.interrupts[i].instruction_index, 4u + 5u * i);  // each NOP, not POPF or IRET
  }
  EXPECT_EQ(r.regs.si, 3);
  EXPECT_EQ(r.regs.di & (TF | IF), 0);  // TF and IF are clear inside the handler
  EXPECT_TRUE(r.regs.flags & TF);
}

TEST(Golden, SegmentLoadInhibitsTrap) {
  // MOV AX,0100h; PUSH AX; POPF (TF=1); MOV ES,AX; NOP
  Program p = boot({0xB8, 0x00, 0x01, 0x50, 0x9D, 0x8E, 0xC0, 0x90});
  add_handler(p, 1, 0x0400, {0xCF});
  const Result r = run(p);
  ASSERT_FALSE(r.interrupts.empty());
  EXPECT_EQ(r.interrupts[0].instruction_index, 5u);  // after the NOP, not after MOV ES
}

TEST(Golden, D11_NoInvalidOpcodeTrap) {
  // MOV AX,0020h; PUSH AX; POP CS -> execution continues at 0020:0105
  Program p = boot({0xB8, 0x20, 0x00, 0x50, 0x0F});
  p.segments.push_back({0x0305, {0xF9, 0xD6, 0x88, 0xC3, 0xF8, 0xD6, 0xF4}});  // STC; SALC; MOV BL,AL; CLC; SALC
  const Result r = run(p);
  EXPECT_EQ(r.stop, StopReason::Halt);
  EXPECT_TRUE(r.interrupts.empty());
  EXPECT_EQ(r.regs.cs, 0x0020);
  EXPECT_EQ(r.regs.ip, 0x010C);
  EXPECT_EQ(r.regs.bx & 0xFF, 0xFF);
  EXPECT_EQ(r.regs.ax & 0xFF, 0x00);
}

// ---- shifts and rotates (D8 table) -----------------------------------------------------------

struct ShiftCase {
  const char* name;
  std::vector<uint8_t> code;
  uint16_t ax;
  uint16_t mask;
  uint16_t flags;
};

class ShiftTable : public ::testing::TestWithParam<ShiftCase> {};

TEST_P(ShiftTable, ResultAndFlags) {
  const ShiftCase& c = GetParam();
  const Result r = run_code(c.code);
  EXPECT_EQ(r.regs.ax, c.ax);
  EXPECT_EQ(r.regs.flags & c.mask, c.flags);
}

constexpr uint16_t kShift1 = CF | OF | SF | ZF | PF;
constexpr uint16_t kShiftN = CF | SF | ZF | PF;
constexpr uint16_t kRot1 = CF | OF;

INSTANTIATE_TEST_SUITE_P(
    Golden, ShiftTable,
    ::testing::Values(
        ShiftCase{"shl16_by16", {0xB8, 0x01, 0x00, 0xB1, 0x10, 0xD3, 0xE0}, 0x0000, kShiftN, CF | ZF | PF},
        ShiftCase{"shl16_by17", {0xB8, 0x01, 0x00, 0xB1, 0x11, 0xD3, 0xE0}, 0x0000, kShiftN, ZF | PF},
        ShiftCase{"shl8_by255", {0xB0, 0xFF, 0xB1, 0xFF, 0xD2, 0xE0}, 0x0000, kShiftN, ZF | PF},
        ShiftCase{"shl8_by0_keeps_flags", {0xB0, 0x01, 0xB1, 0x00, 0xF9, 0xD2, 0xE0}, 0x0001, CF, CF},
        ShiftCase{"shr8_1", {0xB0, 0x81, 0xD0, 0xE8}, 0x0040, kShift1, CF | OF},
        ShiftCase{"sar8_1", {0xB0, 0x81, 0xD0, 0xF8}, 0x00C0, kShift1, CF | SF | PF},
        ShiftCase{"shl8_1_no_of", {0xB0, 0xC0, 0xD0, 0xE0}, 0x0080, kShift1, CF | SF},
        ShiftCase{"shl8_1_of", {0xB0, 0x40, 0xD0, 0xE0}, 0x0080, kShift1, OF | SF},
        ShiftCase{"shr16_by4", {0xB8, 0x08, 0x80, 0xB1, 0x04, 0xD3, 0xE8}, 0x0800, kShiftN, CF | PF},
        ShiftCase{"sar16_by15", {0xB8, 0x00, 0x80, 0xB1, 0x0F, 0xD3, 0xF8}, 0xFFFF, kShiftN, SF | PF},
        ShiftCase{"rol8_1", {0xB0, 0x80, 0xD0, 0xC0}, 0x0001, kRot1, CF | OF},
        ShiftCase{"ror8_1", {0xB0, 0x01, 0xD0, 0xC8}, 0x0080, kRot1, CF | OF},
        ShiftCase{"rcl8_1", {0xF9, 0xB0, 0x00, 0xD0, 0xD0}, 0x0001, kRot1, 0},
        ShiftCase{"rcr8_by9_identity", {0xF9, 0xB0, 0x55, 0xB1, 0x09, 0xD2, 0xD8}, 0x0055, CF, CF},
        ShiftCase{"rol16_by4", {0xB8, 0x34, 0x12, 0xB1, 0x04, 0xD3, 0xC0}, 0x2341, CF, CF},
        ShiftCase{"rotate_keeps_zf", {0x31, 0xC0, 0xB0, 0x01, 0xD0, 0xC0}, 0x0002, ZF | SF, ZF}),
    [](const auto& p) { return std::string(p.param.name); });

// ---- multiply, divide, BCD -------------------------------------------------------------------

TEST(Golden, MulImulFlags) {
  const Result mul = run_code({0xB0, 0x10, 0xB3, 0x10, 0xF6, 0xE3});  // AL=10h; BL=10h; MUL BL
  EXPECT_EQ(mul.regs.ax, 0x0100);
  EXPECT_EQ(mul.regs.flags & (CF | OF), CF | OF);

  const Result imul = run_code({0xB0, 0xFE, 0xB3, 0x03, 0xF6, 0xEB});  // -2 * 3
  EXPECT_EQ(imul.regs.ax, 0xFFFA);
  EXPECT_EQ(imul.regs.flags & (CF | OF), 0);

  // MOV AX,1234h; MOV BX,100h; MUL BX -> DX:AX = 00123400h
  const Result mul16 = run_code({0xB8, 0x34, 0x12, 0xBB, 0x00, 0x01, 0xF7, 0xE3});
  EXPECT_EQ(mul16.regs.ax, 0x3400);
  EXPECT_EQ(mul16.regs.dx, 0x0012);
  EXPECT_EQ(mul16.regs.flags & (CF | OF), CF | OF);
}

TEST(Golden, Divide) {
  const Result div = run_code({0xB8, 0x64, 0x00, 0xB3, 0x07, 0xF6, 0xF3});  // 100 / 7
  EXPECT_EQ(div.regs.ax, 0x020E);

  const Result idiv8 = run_code({0xB8, 0x81, 0xFF, 0xB3, 0x01, 0xF6, 0xFB});  // -127 / 1
  EXPECT_EQ(idiv8.regs.ax, 0x0081);
  EXPECT_TRUE(idiv8.interrupts.empty());

  // MOV DX,FFFFh; MOV AX,FFF9h; MOV BX,2; IDIV BX  ->  -7 / 2 = -3 rem -1
  const Result idiv16 = run_code({0xBA, 0xFF, 0xFF, 0xB8, 0xF9, 0xFF, 0xBB, 0x02, 0x00, 0xF7, 0xFB});
  EXPECT_EQ(idiv16.regs.ax, 0xFFFD);
  EXPECT_EQ(idiv16.regs.dx, 0xFFFF);
}

TEST(Golden, DivideErrors) {
  const std::vector<std::vector<uint8_t>> cases{
      {0xB8, 0x00, 0x10, 0xB3, 0x10, 0xF6, 0xF3},  // DIV: quotient 100h overflows AL
      {0xB8, 0x80, 0xFF, 0xB3, 0x01, 0xF6, 0xFB},  // IDIV: -128 is out of the 8086 range
      {0xD4, 0x00},                                // AAM 0
  };
  for (const auto& code : cases) {
    Program p = boot(code);
    add_handler(p, 0, 0x0200, {0xF4});
    const Result r = run(p);
    ASSERT_EQ(r.interrupts.size(), 1u);
    EXPECT_EQ(r.interrupts[0].type, 0);
    EXPECT_EQ(r.regs.cs, 0);
    EXPECT_EQ(r.regs.ip, 0x0201);
  }
}

TEST(Golden, Bcd) {
  const Result daa = run_code({0xB0, 0x15, 0x04, 0x27, 0x27});  // 15h + 27h = 3Ch; DAA -> 42h
  EXPECT_EQ(daa.regs.ax & 0xFF, 0x42);
  EXPECT_EQ(daa.regs.flags & (AF | CF | ZF | SF | PF), AF | PF);

  const Result das = run_code({0xB0, 0x42, 0x2C, 0x15, 0x2F});  // 42h - 15h = 2Dh; DAS -> 27h
  EXPECT_EQ(das.regs.ax & 0xFF, 0x27);
  EXPECT_EQ(das.regs.flags & (AF | CF), AF);

  const Result aaa = run_code({0xB8, 0x0F, 0x00, 0x37});  // AL=0Fh; AAA
  EXPECT_EQ(aaa.regs.ax, 0x0105);
  EXPECT_EQ(aaa.regs.flags & (AF | CF), AF | CF);

  const Result aam = run_code({0xB0, 0x2F, 0xD4, 0x0A});  // 47 -> AH=4, AL=7
  EXPECT_EQ(aam.regs.ax, 0x0407);

  const Result aad = run_code({0xB8, 0x07, 0x04, 0xD5, 0x0A});  // AH=4, AL=7 -> 47
  EXPECT_EQ(aad.regs.ax, 0x002F);
}

// ---- control flow and interrupts -------------------------------------------------------------

TEST(Golden, LoopSum) {
  // MOV CX,10; XOR AX,AX; L: ADD AX,CX; LOOP L
  const Result r = run_code({0xB9, 0x0A, 0x00, 0x31, 0xC0, 0x01, 0xC8, 0xE2, 0xFC});
  EXPECT_EQ(r.regs.ax, 55);
  EXPECT_EQ(r.regs.cx, 0);
}

TEST(Golden, NearCallRet) {
  // 0100: CALL 0106; 0103: MOV BL,22h; HLT; 0106: MOV AL,11h; RET
  const Result r = run_code({0xE8, 0x03, 0x00, 0xB3, 0x22, 0xF4, 0xB0, 0x11, 0xC3});
  EXPECT_EQ(r.regs.ax & 0xFF, 0x11);
  EXPECT_EQ(r.regs.bx & 0xFF, 0x22);
  EXPECT_EQ(r.regs.sp, 0);
  EXPECT_EQ(r.regs.ip, 0x0106);
  const std::vector<MemWrite> expected{{0xFFFE, 0x03}, {0xFFFF, 0x01}};
  EXPECT_EQ(r.mem_writes, expected);
}

TEST(Golden, FarCallRetf) {
  Program p = boot({0x9A, 0x00, 0x00, 0x20, 0x00});  // CALL 0020:0000
  p.segments.push_back({0x200, {0xB0, 0x33, 0xCB}});  // MOV AL,33h; RETF
  const Result r = run(p);
  EXPECT_EQ(r.regs.ax & 0xFF, 0x33);
  EXPECT_EQ(r.regs.cs, 0);
  EXPECT_EQ(r.regs.ip, 0x0106);
  EXPECT_EQ(r.regs.sp, 0);
}

TEST(Golden, IntIret) {
  Program p = boot({0xFB, 0xCD, 0x21});  // STI; INT 21h
  add_handler(p, 0x21, 0x0300, {0x9C, 0x5B, 0xBA, 0xEF, 0xBE, 0xCF});  // PUSHF; POP BX; MOV DX,BEEFh; IRET
  const Result r = run(p);
  EXPECT_EQ(r.stop, StopReason::Halt);
  ASSERT_EQ(r.interrupts.size(), 1u);
  EXPECT_EQ(r.interrupts[0].type, 0x21);
  EXPECT_EQ(r.interrupts[0].instruction_index, 2u);
  EXPECT_EQ(r.regs.dx, 0xBEEF);
  EXPECT_EQ(r.regs.bx & IF, 0);
  EXPECT_EQ(r.regs.bx & 0xF002, 0xF002);
  EXPECT_TRUE(r.regs.flags & IF);
  EXPECT_EQ(r.regs.ip, 0x0104);
}

TEST(Golden, ConditionalJumps) {
  // MOV AL,5; CMP AL,5; JZ +2; MOV BL,1; JNZ +2; MOV CL,1; JMP +2; MOV DL,1
  const Result r = run_code({0xB0, 0x05, 0x3C, 0x05, 0x74, 0x02, 0xB3, 0x01, 0x75, 0x02, 0xB1,
                             0x01, 0xEB, 0x02, 0xB2, 0x01});
  EXPECT_EQ(r.regs.bx & 0xFF, 0x00);
  EXPECT_EQ(r.regs.cx & 0xFF, 0x01);
  EXPECT_EQ(r.regs.dx & 0xFF, 0x00);

  // 64h is an undocumented alias of 74h (JZ) on the 8086.
  const Result alias = run_code({0xB0, 0x05, 0x3C, 0x05, 0x64, 0x02, 0xB3, 0x01});
  EXPECT_EQ(alias.regs.bx & 0xFF, 0x00);

  // Signed vs unsigned: MOV AL,80h; CMP AL,1; JL +2 (taken); MOV BL,1; JA +2 (taken); MOV CL,1
  const Result sgn = run_code({0xB0, 0x80, 0x3C, 0x01, 0x7C, 0x02, 0xB3, 0x01, 0x77, 0x02, 0xB1, 0x01});
  EXPECT_EQ(sgn.regs.bx & 0xFF, 0x00);
  EXPECT_EQ(sgn.regs.cx & 0xFF, 0x00);
}

// ---- data movement, memory, strings, I/O -----------------------------------------------------

TEST(Golden, IoCapture) {
  Program p = boot({
      0xE4, 0x10,              // IN AL,10h
      0x88, 0xC3,              // MOV BL,AL
      0xBA, 0x20, 0x00, 0xED,  // MOV DX,20h; IN AX,DX
      0x89, 0xC1,              // MOV CX,AX
      0xE4, 0x30,              // IN AL,30h (unmapped)
      0xE6, 0x80,              // OUT 80h,AL
      0xBA, 0x40, 0x00, 0xED,  // MOV DX,40h; IN AX,DX (unmapped)
      0xEF,                    // OUT DX,AX
  });
  p.io_inputs = {{0x10, 0x1234}, {0x20, 0xABCD}};
  const Result r = run(p);
  EXPECT_EQ(r.regs.bx & 0xFF, 0x34);
  EXPECT_EQ(r.regs.cx, 0xABCD);
  ASSERT_EQ(r.io_writes.size(), 2u);
  EXPECT_EQ(r.io_writes[0].port, 0x80);
  EXPECT_EQ(r.io_writes[0].value, 0xFF);
  EXPECT_FALSE(r.io_writes[0].word);
  EXPECT_EQ(r.io_writes[1].port, 0x40);
  EXPECT_EQ(r.io_writes[1].value, 0xFFFF);
  EXPECT_TRUE(r.io_writes[1].word);
}

TEST(Golden, WordAtFFFFWrapsInsideSegment) {
  const Result r = run_code({0xB8, 0x34, 0x12, 0xA3, 0xFF, 0xFF});  // MOV AX,1234h; MOV [FFFFh],AX
  const std::vector<MemWrite> expected{{0x0FFFF, 0x34}, {0x00000, 0x12}};
  EXPECT_EQ(r.mem_writes, expected);
}

TEST(Golden, PushSpPushesDecrementedValue) {
  const Result r = run_code({0xBC, 0x00, 0x10, 0x54, 0x5B});  // MOV SP,1000h; PUSH SP; POP BX
  EXPECT_EQ(r.regs.bx, 0x0FFE);
  EXPECT_EQ(r.regs.sp, 0x1000);
}

TEST(Golden, MovesAndPointers) {
  // MOV BX,10h; MOV SI,20h; LEA AX,[BX+SI+5]; XCHG AX,BX; LDS SI,[0300h]
  Program p = boot({0xBB, 0x10, 0x00, 0xBE, 0x20, 0x00, 0x8D, 0x40, 0x05, 0x93, 0xC5, 0x36,
                    0x00, 0x03});
  p.segments.push_back({0x300, {0x34, 0x12, 0x00, 0x20}});
  const Result r = run(p);
  EXPECT_EQ(r.regs.bx, 0x0035);
  EXPECT_EQ(r.regs.ax, 0x0010);
  EXPECT_EQ(r.regs.si, 0x1234);
  EXPECT_EQ(r.regs.ds, 0x2000);

  const Result conv = run_code({0xB0, 0x80, 0x98, 0x89, 0xC3, 0xB8, 0x00, 0x80, 0x99});  // CBW; CWD
  EXPECT_EQ(conv.regs.bx, 0xFF80);
  EXPECT_EQ(conv.regs.dx, 0xFFFF);
}

TEST(Golden, LahfSahf) {
  const Result lahf = run_code({0xF9, 0x9F});  // STC; LAHF
  EXPECT_EQ(lahf.regs.ax >> 8, 0x03);         // bit 1 reads as 1
  const Result sahf = run_code({0xB4, 0xFF, 0x9E});  // MOV AH,FFh; SAHF
  EXPECT_EQ(sahf.regs.flags, SF | ZF | AF | PF | CF);
}

TEST(Golden, CompareAndScanStrings) {
  // MOV SI,300h; MOV DI,400h; MOV CX,4; REPE CMPSB
  Program cmps = boot({0xBE, 0x00, 0x03, 0xBF, 0x00, 0x04, 0xB9, 0x04, 0x00, 0xF3, 0xA6});
  cmps.segments.push_back({0x300, {'A', 'B', 'C', 'D'}});
  cmps.segments.push_back({0x400, {'A', 'B', 'X', 'D'}});
  const Result rc = run(cmps);
  EXPECT_EQ(rc.regs.cx, 1);
  EXPECT_EQ(rc.regs.si, 0x0303);
  EXPECT_EQ(rc.regs.di, 0x0403);
  EXPECT_FALSE(rc.regs.flags & ZF);

  // MOV AL,'C'; MOV DI,300h; MOV CX,4; REPNE SCASB
  Program scas = boot({0xB0, 'C', 0xBF, 0x00, 0x03, 0xB9, 0x04, 0x00, 0xF2, 0xAE});
  scas.segments.push_back({0x300, {'A', 'B', 'C', 'D'}});
  const Result rs = run(scas);
  EXPECT_EQ(rs.regs.cx, 1);
  EXPECT_EQ(rs.regs.di, 0x0303);
  EXPECT_TRUE(rs.regs.flags & ZF);

  // MOV SI,300h; LODSW; MOV DI,500h; MOV CX,2; REP STOSW; ES: LODSB (ES=0, so same memory)
  Program ls = boot({0xBE, 0x00, 0x03, 0xAD, 0xBF, 0x00, 0x05, 0xB9, 0x02, 0x00, 0xF3, 0xAB,
                     0x26, 0xAC});
  ls.segments.push_back({0x300, {0x11, 0x22, 0x33}});
  const Result rl = run(ls);
  EXPECT_EQ(rl.regs.ax, 0x2233);
  EXPECT_EQ(rl.regs.si, 0x0303);
  const std::vector<MemWrite> expected{{0x500, 0x11}, {0x501, 0x22}, {0x502, 0x11}, {0x503, 0x22}};
  EXPECT_EQ(rl.mem_writes, expected);

  const Result zero = run_code({0xB9, 0x00, 0x00, 0xF3, 0xAA});  // REP STOSB with CX=0
  EXPECT_TRUE(zero.mem_writes.empty());
}

TEST(Golden, LogicClearsCarryAndOverflow) {
  // MOV AL,FFh; ADD AL,1 (CF=1); OR AL,80h
  const Result r = run_code({0xB0, 0xFF, 0x04, 0x01, 0x0C, 0x80});
  EXPECT_EQ(r.regs.ax & 0xFF, 0x80);
  EXPECT_EQ(r.regs.flags & (CF | OF | SF | ZF | PF), SF);
}

}  // namespace
}  // namespace i8086::golden
