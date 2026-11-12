// White-box per-instruction tests for the v1 subset, using the brief §7 D-test vectors.

#include <gtest/gtest.h>

#include "harness.hpp"

namespace i8086::model::test {
namespace {

constexpr uint16_t CF = 0x0001, PF = 0x0004, AF = 0x0010, ZF = 0x0040, SF = 0x0080, TF = 0x0100,
                   IF = 0x0200, DF = 0x0400, OF = 0x0800;

ArchState run(const std::vector<uint8_t>& code, Harness* hp = nullptr) {
  Harness local;
  Harness& h = hp ? *hp : local;
  h.boot(code);
  h.reset();
  EXPECT_TRUE(h.run_until_halt());
  return h.arch();
}

uint16_t word_at(const Harness& h, uint32_t a) {
  return static_cast<uint16_t>(h.mem[a] | (h.mem[a + 1] << 8));
}

std::vector<Txn> writes(const Harness& h) {
  std::vector<Txn> w;
  for (const Txn& t : h.txns) {
    if (t.type == BusCycleType::MemWrite) w.push_back(t);
  }
  return w;
}

TEST(Instr, D1_AddFlags) {
  const ArchState a = run({0xB0, 0x7F, 0x04, 0x01});  // MOV AL,7Fh ; ADD AL,1
  EXPECT_EQ(a.ax & 0xFF, 0x80);
  EXPECT_TRUE(a.flags & OF);
  EXPECT_TRUE(a.flags & SF);
  EXPECT_FALSE(a.flags & ZF);
  EXPECT_TRUE(a.flags & AF);
  EXPECT_FALSE(a.flags & PF);
  EXPECT_FALSE(a.flags & CF);
}

TEST(Instr, D2_IncPreservesCarry) {
  const ArchState a = run({0xF9, 0x40});  // STC ; INC AX
  EXPECT_EQ(a.ax, 1);
  EXPECT_TRUE(a.flags & CF);
}

TEST(Instr, D3_DivideErrorStacksIpPastDiv) {
  Harness h;
  h.load(0x0000, {0x00, 0x00, 0x00, 0x03});  // INT 0 -> 0300:0000
  h.load(0x3000, {0xF4});
  // MOV AX,1 ; MOV BL,0 ; DIV BL
  const ArchState a = run({0xB8, 0x01, 0x00, 0xB3, 0x00, 0xF6, 0xF3}, &h);
  EXPECT_EQ(a.cs, 0x0300);
  EXPECT_EQ(a.ip, 0x0001);
  EXPECT_EQ(a.sp, 0x00FA);
  const auto w = writes(h);
  ASSERT_EQ(w.size(), 3u);  // FLAGS, CS, IP
  EXPECT_EQ(w[1].data, 0x0100);
  EXPECT_EQ(w[2].data, Harness::kPrologueLen + 7);  // IP after the 2-byte DIV
  EXPECT_EQ(w[2].addr, 0x200FAu);
  EXPECT_EQ(w[2].s4s3, 0b01);  // stack writes use SS
}

TEST(Instr, DivComputesQuotientAndRemainder) {
  // MOV AX,0107h ; MOV BL,10h ; DIV BL -> AL=10h, AH=07h ; MOV DX,1 ; MOV AX,2 ; MOV CX,3 ; DIV CX
  const ArchState a =
      run({0xB8, 0x07, 0x01, 0xB3, 0x10, 0xF6, 0xF3, 0x88, 0xC7, 0xBA, 0x01, 0x00, 0xB8, 0x02,
           0x00, 0xB9, 0x03, 0x00, 0xF7, 0xF1});
  EXPECT_EQ(a.bx >> 8, 0x10);  // MOV BH,AL saved the byte quotient
  EXPECT_EQ(a.ax, 0x5556);     // 0x10002 / 3
  EXPECT_EQ(a.dx, 0x0000);
}

TEST(Instr, D4_SegmentDefaultsOnStatusPins) {
  Harness h;
  // MOV AX,[BP+2] ; MOV AX,[BX+2] ; MOV AX,ES:[BX+2]
  run({0x8B, 0x46, 0x02, 0x8B, 0x47, 0x02, 0x26, 0x8B, 0x47, 0x02}, &h);
  std::vector<Txn> reads;
  for (const Txn& t : h.txns) {
    if (t.type == BusCycleType::MemRead) reads.push_back(t);
  }
  ASSERT_EQ(reads.size(), 3u);
  EXPECT_EQ(reads[0].s4s3, 0b01);  // SS
  EXPECT_EQ(reads[0].addr, 0x20002u);
  EXPECT_EQ(reads[1].s4s3, 0b11);  // DS
  EXPECT_EQ(reads[2].s4s3, 0b00);  // ES
}

TEST(Instr, D5_SegmentWrapsAt1MiB) {
  Harness h;
  // MOV AX,FFFFh ; MOV ES,AX ; MOV BYTE ES:[0010h],AAh
  run({0xB8, 0xFF, 0xFF, 0x8E, 0xC0, 0x26, 0xC6, 0x06, 0x10, 0x00, 0xAA}, &h);
  const auto w = writes(h);
  ASSERT_EQ(w.size(), 1u);
  EXPECT_EQ(w[0].addr, 0x00000u);
  EXPECT_EQ(w[0].data, 0xAA);
}

TEST(Instr, D6_RepMovswDownward) {
  Harness h;
  h.load(0x200, {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88});
  // STD ; MOV CX,4 ; MOV SI,0206h ; MOV DI,0306h ; REP MOVSW
  const ArchState a =
      run({0xFD, 0xB9, 0x04, 0x00, 0xBE, 0x06, 0x02, 0xBF, 0x06, 0x03, 0xF3, 0xA5}, &h);
  EXPECT_EQ(a.cx, 0);
  EXPECT_EQ(a.si, 0x01FE);
  EXPECT_EQ(a.di, 0x02FE);
  const auto w = writes(h);
  ASSERT_EQ(w.size(), 4u);
  EXPECT_EQ(w[0].addr, 0x306u);
  EXPECT_EQ(w[0].data, 0x8877);
  EXPECT_EQ(w[3].addr, 0x300u);
  EXPECT_EQ(w[3].data, 0x2211);
  EXPECT_EQ(word_at(h, 0x300), 0x2211);
  EXPECT_EQ(word_at(h, 0x306), 0x8877);
}

TEST(Instr, RepStosbWithZeroCountDoesNothing) {
  Harness h;
  // MOV CX,0 ; REP STOSB ; MOV CX,3 ; MOV AL,5 ; REP STOSB
  const ArchState a = run({0xB9, 0x00, 0x00, 0xF3, 0xAA, 0xB9, 0x03, 0x00, 0xB0, 0x05, 0xF3, 0xAA}, &h);
  EXPECT_EQ(a.di, 3);
  EXPECT_EQ(writes(h).size(), 3u);
}

TEST(Instr, D8_ShiftCountIsNotMasked) {
  // MOV AX,1 ; MOV CL,33 ; SHL AX,CL
  const ArchState a = run({0xB8, 0x01, 0x00, 0xB1, 0x21, 0xD3, 0xE0});
  EXPECT_EQ(a.ax, 0);
  EXPECT_TRUE(a.flags & ZF);
}

TEST(Instr, ShiftsSetCarryAndOverflow) {
  // MOV AL,81h ; SHL AL,1 -> 02h, CF=1, OF=1 ; MOV BL,81h ; SAR BL,1 -> C0h ; MOV DL,81h ; SHR DL,1
  const ArchState a = run({0xB0, 0x81, 0xD0, 0xE0, 0xB3, 0x81, 0xD0, 0xFB, 0xB2, 0x81, 0xD0, 0xEA});
  EXPECT_EQ(a.ax & 0xFF, 0x02);
  EXPECT_EQ(a.bx & 0xFF, 0xC0);
  EXPECT_EQ(a.dx & 0xFF, 0x40);
  EXPECT_TRUE(a.flags & CF);
  EXPECT_TRUE(a.flags & OF);  // SHR by 1: OF = original MSB
}

TEST(Instr, D9_PushfReadsUpperBitsAsOnes) {
  // XOR AX,AX ; PUSH AX ; POPF ; PUSHF ; POP AX
  const ArchState a = run({0x31, 0xC0, 0x50, 0x9D, 0x9C, 0x58});
  EXPECT_EQ(a.ax & 0xF000, 0xF000);
  EXPECT_EQ(a.ax, 0xF002);
}

TEST(Instr, D11_PopCsAndD6DoNotTrap) {
  Harness h;
  h.load(0x0018, {0x00, 0x00, 0x00, 0x07});  // a type-6 vector, which must never be used
  h.load(0x7000, {0xF4});
  // MOV AX,0400h ; PUSH AX ; POP CS -> continues at 0400:(IP after 0F)
  const uint16_t ip_after = Harness::kPrologueLen + 5;
  h.load(0x4000 + ip_after, {0xF9, 0xD6, 0xF4});  // STC ; D6 (SALC) ; HLT
  const ArchState a = run({0xB8, 0x00, 0x04, 0x50, 0x0F}, &h);
  EXPECT_EQ(a.cs, 0x0400);
  EXPECT_EQ(a.ip, ip_after + 3);
  EXPECT_EQ(a.ax & 0xFF, 0xFF);
  EXPECT_EQ(a.sp, 0x0100);
}

TEST(Instr, AluFamilies) {
  // MOV AX,00F0h ; MOV BX,0F0Fh ; AND AX,BX -> 0000 ; OR BX,1000h -> 1F0F ; XOR CX,CX
  // STC ; ADC CX,5 -> 6 ; SBB CX,BX -> 6-1F0F ; CMP CX,CX
  const ArchState a = run({0xB8, 0xF0, 0x00, 0xBB, 0x0F, 0x0F, 0x21, 0xD8, 0x81, 0xCB, 0x00,
                           0x10, 0x31, 0xC9, 0xF9, 0x83, 0xD1, 0x05, 0x19, 0xD9, 0x39, 0xC9});
  EXPECT_EQ(a.ax, 0x0000);
  EXPECT_EQ(a.bx, 0x1F0F);
  EXPECT_EQ(a.cx, static_cast<uint16_t>(6 - 0x1F0F));
  EXPECT_TRUE(a.flags & ZF);  // CMP CX,CX
  EXPECT_FALSE(a.flags & CF);
}

TEST(Instr, SubBorrowAndMemoryOperand) {
  Harness h;
  h.load(0x0400, {0x01, 0x00});
  // MOV BX,0400h ; SUB WORD [BX],2 ; DEC BYTE [BX+1]
  run({0xBB, 0x00, 0x04, 0x83, 0x2F, 0x02, 0xFE, 0x4F, 0x01}, &h);
  const auto w = writes(h);
  ASSERT_EQ(w.size(), 2u);
  EXPECT_EQ(w[0].data, 0xFFFF);
  EXPECT_EQ(w[1].addr, 0x401u);
}

TEST(Instr, JccLoopCounts) {
  // MOV CX,3 ; L: INC BX ; DEC CX ; JNZ L
  const ArchState a = run({0xB9, 0x03, 0x00, 0x43, 0x49, 0x75, 0xFC});
  EXPECT_EQ(a.bx, 3);
  EXPECT_EQ(a.cx, 0);
}

TEST(Instr, NearAndFarCallReturn) {
  Harness h;
  h.load(0x6000, {0xBA, 0x22, 0x22, 0xCB});  // 0600:0000: MOV DX,2222h ; RETF
  // CALL sub ; CALL FAR 0600:0000 ; HLT ; sub: MOV BX,1111h ; RET
  const ArchState a =
      run({0xE8, 0x06, 0x00, 0x9A, 0x00, 0x00, 0x00, 0x06, 0xF4, 0xBB, 0x11, 0x11, 0xC3}, &h);
  EXPECT_EQ(a.bx, 0x1111);
  EXPECT_EQ(a.dx, 0x2222);
  EXPECT_EQ(a.cs, 0x0100);
  EXPECT_EQ(a.sp, 0x0100);
}

TEST(Instr, IndirectJumpAndCall) {
  Harness h;
  h.load(0x0500, {0x00, 0x00, 0x00, 0x06});  // far pointer 0600:0000
  h.load(0x6000, {0xF4});
  // MOV BX,0500h ; JMP FAR [BX]
  const ArchState a = run({0xBB, 0x00, 0x05, 0xFF, 0x2F}, &h);
  EXPECT_EQ(a.cs, 0x0600);
  EXPECT_EQ(a.ip, 1);
}

TEST(Instr, IntAndIret) {
  Harness h;
  h.load(0x21 * 4, {0x00, 0x00, 0x00, 0x05});  // INT 21h -> 0500:0000
  h.load(0x5000, {0xBA, 0xEF, 0xBE, 0xCF});    // MOV DX,BEEFh ; IRET
  // STI ; INT 21h
  const ArchState a = run({0xFB, 0xCD, 0x21}, &h);
  EXPECT_EQ(a.dx, 0xBEEF);
  EXPECT_EQ(a.cs, 0x0100);
  EXPECT_EQ(a.sp, 0x0100);
  EXPECT_TRUE(a.flags & IF);  // restored by IRET
  const auto w = writes(h);
  ASSERT_EQ(w.size(), 3u);
  EXPECT_TRUE(w[0].data & IF);  // FLAGS pushed before IF was cleared
}

TEST(Instr, PushPopAndSegmentMoves) {
  // MOV AX,1234h ; PUSH AX ; POP DS ; MOV BX,DS ; PUSH SP ; POP CX ; PUSH ES ; POP DX
  const ArchState a = run({0xB8, 0x34, 0x12, 0x50, 0x1F, 0x8C, 0xDB, 0x54, 0x59, 0x06, 0x5A});
  EXPECT_EQ(a.ds, 0x1234);
  EXPECT_EQ(a.bx, 0x1234);
  EXPECT_EQ(a.cx, 0x00FE);  // PUSH SP stores the decremented SP on the 8086
  EXPECT_EQ(a.dx, 0x0000);
  EXPECT_EQ(a.sp, 0x0100);
}

TEST(Instr, MovFormsAndFlagControl) {
  Harness h;
  // MOV WORD [0600h],ABCDh ; MOV SI,[0600h] ; MOV DI,SI ; STD ; CLD ; STC ; CLC ; CLI
  const ArchState a = run({0xC7, 0x06, 0x00, 0x06, 0xCD, 0xAB, 0x8B, 0x36, 0x00, 0x06, 0x89,
                           0xF7, 0xFD, 0xFC, 0xF9, 0xF8, 0xFA},
                          &h);
  EXPECT_EQ(a.si, 0xABCD);
  EXPECT_EQ(a.di, 0xABCD);
  EXPECT_FALSE(a.flags & (DF | CF | IF | TF));
  const auto w = writes(h);
  ASSERT_EQ(w.size(), 1u);
  EXPECT_EQ(w[0].data, 0xABCD);
}

TEST(Instr, SingleStepTrapsAfterEachInstruction) {
  Harness h;
  h.load(0x0004, {0x00, 0x00, 0x00, 0x08});  // type 1 -> 0800:0000: INC BP ; IRET
  h.load(0x8000, {0x45, 0xCF});
  // PUSHF ; POP AX ; OR AH,01h ; PUSH AX ; POPF ; NOP ; NOP ; NOP ; (trap runs after each)
  // then clear TF: PUSHF ; POP AX ; AND AH,FEh ; PUSH AX ; POPF
  const ArchState a = run({0x9C, 0x58, 0x80, 0xCC, 0x01, 0x50, 0x9D, 0x90, 0x90, 0x90, 0x9C, 0x58,
                           0x80, 0xE4, 0xFE, 0x50, 0x9D},
                          &h);
  EXPECT_FALSE(a.flags & TF);
  // Traps after NOP, NOP, NOP, PUSHF, POP, AND, PUSH, POPF (TF still set at its start).
  EXPECT_EQ(a.bp, 8);
}

TEST(Instr, OutsideSubsetRaisesUnimplemented) {
  Harness h;
  h.boot({0x27});  // DAA
  h.reset();
  EXPECT_THROW(
      {
        for (int i = 0; i < 1000; ++i) h.clock();
      },
      UnimplementedInstruction);
  // The model stays failed until RESET.
  EXPECT_THROW(h.clock(), UnimplementedInstruction);
  h.reset();
  h.clock();
}

// A tester's next run starts with RESET undriven at the first edge: that edge neither
// clears the fault nor raises it again; RESET low raises it, RESET high clears it.
TEST(Instr, UndrivenResetNeitherClearsNorRaisesAFault) {
  Harness h;
  h.boot({0x27});
  h.reset();
  EXPECT_THROW(
      {
        for (int i = 0; i < 1000; ++i) h.clock();
      },
      UnimplementedInstruction);
  const InputSample undriven{h.pin("RESET"), Logic::Z, -1000};
  h.dut->set_inputs({&undriven, 1});
  EXPECT_TRUE(h.dut->clock_edge(Edge::Falling).empty());
  const InputSample low{h.pin("RESET"), Logic::L0, -1000};
  h.dut->set_inputs({&low, 1});
  EXPECT_THROW(h.dut->clock_edge(Edge::Rising), UnimplementedInstruction);
  h.boot({0x90});
  h.reset();
  EXPECT_TRUE(h.run_until_halt());
}

}  // namespace
}  // namespace i8086::model::test
