
//===-- LTHLAsmParser.cpp - Parse LTHL assembly to MCInst instructions --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/LTHLMCTargetDesc.h"
#include "TargetInfo/LTHLTargetInfo.h"

#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCParser/AsmLexer.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"

using namespace llvm;

namespace {

class LTHLOperand;

enum class LTHLShiftKind { SHL, SRL, SRA };

class LTHLAsmParser : public MCTargetAsmParser {
  MCAsmParser &Parser;

  bool matchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                                OperandVector &Operands, MCStreamer &Out,
                                uint64_t &ErrorInfo,
                                bool MatchingInlineAsm) override;

  bool parseRegister(MCRegister &Reg, SMLoc &StartLoc,
                      SMLoc &EndLoc) override;
  ParseStatus tryParseRegister(MCRegister &Reg, SMLoc &StartLoc,
                                SMLoc &EndLoc) override;

  bool parseInstruction(ParseInstructionInfo &Info, StringRef Name,
                         SMLoc NameLoc, OperandVector &Operands) override;

  bool parseOperand(OperandVector &Operands);

  // "call $addr" pseudo-mnemonic expansion -- see its definition's
  // comment for what it expands into and why it can't be a plain
  // InstAlias.
  bool expandCall(OperandVector &Operands, SMLoc IDLoc, MCStreamer &Out);

  // "shl/srl/sra $rd, $rs1, $amt" pseudo-mnemonic expansion
  bool expandShift(OperandVector &Operands, SMLoc IDLoc, MCStreamer &Out,
                    LTHLShiftKind Kind);

  MCAsmParser &getParser() const { return Parser; }
  AsmLexer &getLexer() const { return Parser.getLexer(); }

/// Auto-generated matcher functions (mnemonic/operand-class dispatch
/// table, ComputeAvailableFeatures, MatchInstructionImpl, ...).
#define GET_ASSEMBLER_HEADER
#include "LTHLGenAsmMatcher.inc"

public:
  /// Target-specific match-failure diagnostic kinds, one per custom
  /// operand match class that can "near-match" (parses fine as an
  /// expression/immediate but fails the class's own range check --
  /// here, SImm24AsmOperand::isSImm24()). Seeded from
  /// FIRST_TARGET_MATCH_RESULT_TY and extended by the
  /// GET_OPERAND_DIAGNOSTIC_TYPES block of the generated matcher,
  /// which is where Match_SImm24 actually comes from. Must be public:
  /// the free function getMatchKindDiag() below (generated into this
  /// same .inc under GET_MATCHER_IMPLEMENTATION) references
  /// LTHLAsmParser::Match_SImm24 from outside the class.
  enum LTHLMatchResultTy {
    Match_Dummy = FIRST_TARGET_MATCH_RESULT_TY,
#define GET_OPERAND_DIAGNOSTIC_TYPES
#include "LTHLGenAsmMatcher.inc"
#undef GET_OPERAND_DIAGNOSTIC_TYPES
  };

  LTHLAsmParser(const MCSubtargetInfo &STI, MCAsmParser &Parser,
                const MCInstrInfo &MII)
      : MCTargetAsmParser(STI, MII), Parser(Parser) {
    MCAsmParserExtension::Initialize(Parser);
    setAvailableFeatures(ComputeAvailableFeatures(STI.getFeatureBits()));
  }
};

/// A single parsed LTHL assembly operand -- a register, an immediate/
/// label expression, or (as the very first operand of every
/// instruction) the mnemonic token itself. No memory-operand kind: see
/// the file comment for why LTHL doesn't need one.
class LTHLOperand : public MCParsedAsmOperand {
  enum KindTy { k_Reg, k_Imm, k_Tok } Kind;

  union {
    MCRegister Reg;
    const MCExpr *Imm;
    StringRef Tok;
  };

  SMLoc Start, End;

public:
  LTHLOperand(StringRef Tok, SMLoc S) : Kind(k_Tok), Tok(Tok), Start(S), End(S) {}
  LTHLOperand(MCRegister Reg, SMLoc S, SMLoc E)
      : Kind(k_Reg), Reg(Reg), Start(S), End(E) {}
  LTHLOperand(const MCExpr *Imm, SMLoc S, SMLoc E)
      : Kind(k_Imm), Imm(Imm), Start(S), End(E) {}

  static std::unique_ptr<LTHLOperand> CreateToken(StringRef Str, SMLoc S) {
    return std::make_unique<LTHLOperand>(Str, S);
  }
  static std::unique_ptr<LTHLOperand> CreateReg(MCRegister Reg, SMLoc S,
                                                 SMLoc E) {
    return std::make_unique<LTHLOperand>(Reg, S, E);
  }
  static std::unique_ptr<LTHLOperand> CreateImm(const MCExpr *Val, SMLoc S,
                                                 SMLoc E) {
    return std::make_unique<LTHLOperand>(Val, S, E);
  }

  bool isReg() const override { return Kind == k_Reg; }
  bool isImm() const override { return Kind == k_Imm; }
  bool isToken() const override { return Kind == k_Tok; }
  bool isMem() const override { return false; }

  // SImm24AsmOperand's PredicateMethod (see LTHLInstrInfo.td's simm24
  // def): accepts any non-constant expression unconditionally (there's
  // no relocation support yet to range-check a symbol reference
  // against), and range-checks a compile-time constant against 24
  // signed bits.
  bool isSImm24() const {
    if (Kind != k_Imm)
      return false;
    int64_t Val;
    if (!Imm->evaluateAsAbsolute(Val))
      return true;
    return isInt<24>(Val);
  }

  MCRegister getReg() const override {
    assert(Kind == k_Reg && "Invalid access!");
    return Reg;
  }

  StringRef getToken() const {
    assert(Kind == k_Tok && "Invalid access!");
    return Tok;
  }

  const MCExpr *getImmExpr() const {
    assert(Kind == k_Imm && "Invalid access!");
    return Imm;
  }

  SMLoc getStartLoc() const override { return Start; }
  SMLoc getEndLoc() const override { return End; }

  void addRegOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createReg(Reg));
  }

  void addImmOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    // Render as a plain immediate when the expression is already a
    // compile-time constant (the overwhelmingly common case: literal
    // numbers); otherwise carry the MCExpr through unevaluated (a
    // forward-referenced or external label) for a later pass -- there's
    // no MCCodeEmitter yet to actually resolve/encode it against.
    if (const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(Imm))
      Inst.addOperand(MCOperand::createImm(CE->getValue()));
    else
      Inst.addOperand(MCOperand::createExpr(Imm));
  }

  void print(raw_ostream &O, const MCAsmInfo &MAI) const override {
    switch (Kind) {
    case k_Tok:
      O << "Token " << Tok;
      break;
    case k_Reg:
      O << "Register " << Reg.id();
      break;
    case k_Imm:
      O << "Immediate ";
      MAI.printExpr(O, *Imm);
      break;
    }
  }
};

} // end anonymous namespace

// Auto-generated by TableGen from LTHLRegisterInfo.td's register names.
static MCRegister MatchRegisterName(StringRef Name);

bool LTHLAsmParser::parseRegister(MCRegister &Reg, SMLoc &StartLoc,
                                   SMLoc &EndLoc) {
  ParseStatus Res = tryParseRegister(Reg, StartLoc, EndLoc);
  if (Res.isFailure())
    return Error(StartLoc, "invalid register name");
  if (Res.isSuccess())
    return false;
  if (Res.isNoMatch())
    return true;
  llvm_unreachable("unknown parse status");
}

ParseStatus LTHLAsmParser::tryParseRegister(MCRegister &Reg, SMLoc &StartLoc,
                                             SMLoc &EndLoc) {
  if (getLexer().getKind() != AsmToken::Identifier)
    return ParseStatus::Failure;

  StringRef Name = getLexer().getTok().getIdentifier();
  MCRegister R = MatchRegisterName(Name);
  if (!R)
    return ParseStatus::NoMatch; // Not a register -- e.g. a label.

  Reg = R;
  StartLoc = getLexer().getTok().getLoc();
  EndLoc = getLexer().getTok().getEndLoc();
  getLexer().Lex(); // Eat the register token.
  return ParseStatus::Success;
}

bool LTHLAsmParser::parseOperand(OperandVector &Operands) {
  // Try a register first; tryParseRegister leaves the lexer untouched
  // on NoMatch, so falling through to expression parsing below is safe
  // and handles the common case this exists for: a label name that
  // isn't also a register name (e.g. a branch target).
  if (getLexer().is(AsmToken::Identifier)) {
    MCRegister Reg;
    SMLoc StartLoc, EndLoc;
    ParseStatus Res = tryParseRegister(Reg, StartLoc, EndLoc);
    if (Res.isFailure())
      return true;
    if (Res.isSuccess()) {
      Operands.push_back(LTHLOperand::CreateReg(Reg, StartLoc, EndLoc));
      return false;
    }
  }

  // Otherwise: a numeric literal or a symbol reference (both immediates
  // and branch targets go through this same path -- see the file
  // comment on why there's no separate memory/PC-relative syntax).
  SMLoc StartLoc = getLexer().getLoc();
  const MCExpr *Val;
  if (getParser().parseExpression(Val))
    return true;
  SMLoc EndLoc = getLexer().getLoc();
  Operands.push_back(LTHLOperand::CreateImm(Val, StartLoc, EndLoc));
  return false;
}

bool LTHLAsmParser::parseInstruction(ParseInstructionInfo &Info,
                                      StringRef Name, SMLoc NameLoc,
                                      OperandVector &Operands) {
  Operands.push_back(LTHLOperand::CreateToken(Name, NameLoc));

  if (getLexer().is(AsmToken::EndOfStatement))
    return false;

  if (parseOperand(Operands))
    return true;

  while (parseOptionalToken(AsmToken::Comma)) {
    if (parseOperand(Operands))
      return true;
  }

  if (getLexer().isNot(AsmToken::EndOfStatement)) {
    SMLoc Loc = getLexer().getLoc();
    getParser().eatToEndOfStatement();
    return Error(Loc, "unexpected token");
  }

  getParser().Lex(); // Consume the EndOfStatement.
  return false;
}

// "call $addr" isn't a real LTHLInstrInfo.td instruction -- LTHL has no
// jump-and-link opcode (see LTHLInstrInfo.td's CALL_PSEUDO/ADDPC
// comments). This hand-expands it into the same four real instructions
//
// Clobbers r26 (link register -- expected, same as a real call), r28,
// r29, and FLAGS (ADDPC's ADD-family side effect). r28 joins r29/r30 as
// a register hand-written code using `call` needs to treat as
// reserved/clobbered, not general-purpose.
bool LTHLAsmParser::expandCall(OperandVector &Operands, SMLoc IDLoc,
                                MCStreamer &Out) {
  if (Operands.size() != 2)
    return Error(IDLoc,
                 "call requires exactly one operand: the address to call");

  auto &AddrOp = static_cast<LTHLOperand &>(*Operands[1]);
  if (!AddrOp.isImm())
    return Error(AddrOp.getStartLoc(), "call's operand must be an address");
  if (!AddrOp.isSImm24())
    return Error(AddrOp.getStartLoc(),
                 "address must fit in a signed 24-bit integer");

  auto emit = [&](unsigned Opc, std::initializer_list<MCOperand> Ops) {
    MCInst MI;
    MI.setOpcode(Opc);
    for (const MCOperand &Op : Ops)
      MI.addOperand(Op);
    MI.setLoc(IDLoc);
    Out.emitInstruction(MI, *STI);
  };

  { // ld r28, $addr -- AddrOp renders const-vs-symbol itself
    MCInst MI;
    MI.setOpcode(LTHL::LD);
    MI.addOperand(MCOperand::createReg(LTHL::R28));
    AddrOp.addImmOperands(MI, 1);
    MI.setLoc(IDLoc);
    Out.emitInstruction(MI, *STI);
  }
  emit(LTHL::LD, {MCOperand::createReg(LTHL::R29), MCOperand::createImm(8)});
  emit(LTHL::ADDPC,
       {MCOperand::createReg(LTHL::R26), MCOperand::createReg(LTHL::R29)});
  emit(LTHL::JR, {MCOperand::createReg(LTHL::R28)});
  return false;
}

// "shl/srl/sra $rd, $rs1, $amt" -- like `call` above, none of these are
// real LTHLInstrInfo.td instructions; LTHL's ALU has no shift
// instruction at all
bool LTHLAsmParser::expandShift(OperandVector &Operands, SMLoc IDLoc,
                                 MCStreamer &Out, LTHLShiftKind Kind) {
  if (Operands.size() != 4)
    return Error(IDLoc, "shift requires exactly three operands: "
                         "destination, source, and amount");

  auto &RdOp = static_cast<LTHLOperand &>(*Operands[1]);
  auto &RsOp = static_cast<LTHLOperand &>(*Operands[2]);
  auto &AmtOp = static_cast<LTHLOperand &>(*Operands[3]);

  if (!RdOp.isReg())
    return Error(RdOp.getStartLoc(), "expected destination register");
  if (!RsOp.isReg())
    return Error(RsOp.getStartLoc(), "expected source register");
  if (!AmtOp.isReg() && !AmtOp.isImm())
    return Error(AmtOp.getStartLoc(),
                 "shift amount must be a register or an immediate");

  MCRegister Rd = RdOp.getReg();
  MCRegister Rs = RsOp.getReg();

  auto emit = [&](unsigned Opc, std::initializer_list<MCOperand> Ops) {
    MCInst MI;
    MI.setOpcode(Opc);
    for (const MCOperand &Op : Ops)
      MI.addOperand(Op);
    MI.setLoc(IDLoc);
    Out.emitInstruction(MI, *STI);
  };

  // One bit of shift, applied in place to Rd -- the exact same ALU
  // sequences emitShift's LoopBB builds (see its comment in
  // LTHLISelLowering.cpp), just operating on a real register instead of
  // a fresh vreg pair, since hand-written asm has no SSA/PHI to thread
  // a Val/NextVal split through.
  auto emitBitOnce = [&]() {
    switch (Kind) {
    case LTHLShiftKind::SHL:
      // Rd += Rd -- doubling; mod-2^32 wraparound is exactly a left
      // shift by one.
      emit(LTHL::ADD, {MCOperand::createReg(Rd), MCOperand::createReg(Rd),
                        MCOperand::createReg(Rd)});
      break;
    case LTHLShiftKind::SRL:
      // Clear Carry (writing the discarded sum to r0 -- see the doc's
      // NOP encoding, add r0,r0,r0 -- still costs a real ALU cycle and
      // sets FLAGS, it just doesn't have to land anywhere), then rotate
      // Rd through it: the cleared carry fills the vacated top bit with
      // 0.
      emit(LTHL::ADD, {MCOperand::createReg(LTHL::R0),
                        MCOperand::createReg(LTHL::R0),
                        MCOperand::createReg(LTHL::R0)});
      emit(LTHL::ROR, {MCOperand::createReg(Rd), MCOperand::createReg(Rd),
                        MCOperand::createReg(LTHL::R0)});
      break;
    case LTHLShiftKind::SRA:
      // Self-add Rd into r0 (discarding the doubled sum -- only its
      // Carry out matters, which equals Rd's original MSB), then rotate
      // Rd itself through that carry: the saved sign bit rotates back
      // in as the new top bit.
      emit(LTHL::ADD, {MCOperand::createReg(LTHL::R0),
                        MCOperand::createReg(Rd), MCOperand::createReg(Rd)});
      emit(LTHL::ROR, {MCOperand::createReg(Rd), MCOperand::createReg(Rd),
                        MCOperand::createReg(LTHL::R0)});
      break;
    }
  };

  if (AmtOp.isImm()) {
    int64_t Amt;
    if (!AmtOp.getImmExpr()->evaluateAsAbsolute(Amt))
      return Error(AmtOp.getStartLoc(),
                   "shift amount must be a compile-time constant when not "
                   "a register -- there's no relocation support for a "
                   "symbolic unroll count");
    if (Amt < 0 || Amt > 31)
      return Error(AmtOp.getStartLoc(),
                   "shift amount must be between 0 and 31");

    // Rd := Rs, then Amt bits of shift in place -- correct (a no-op
    // loop body) even for Amt == 0.
    emit(LTHL::ADD, {MCOperand::createReg(Rd), MCOperand::createReg(Rs),
                      MCOperand::createReg(LTHL::R0)});
    for (int64_t I = 0; I < Amt; ++I)
      emitBitOnce();
    return false;
  }

  // Register $amt: the runtime-loop path -- see this function's
  // top-of-function comment for the r28/r29 scratch convention.
  MCRegister Amt = AmtOp.getReg();
  if (Rd == LTHL::R28 || Rd == LTHL::R29)
    return Error(RdOp.getStartLoc(),
                 "destination register can't be r28 or r29 when the shift "
                 "amount is a register -- shl/srl/sra use those as "
                 "internal scratch registers for the runtime loop");
  if (Amt == LTHL::R28 || Amt == LTHL::R29)
    return Error(AmtOp.getStartLoc(),
                 "shift-amount register can't be r28 or r29 -- shl/srl/sra "
                 "use those as internal scratch registers for the runtime "
                 "loop");
  if (Rd == Amt)
    return Error(RdOp.getStartLoc(),
                 "destination and shift-amount registers must differ -- "
                 "the destination is written (from the source register) "
                 "before the amount is tested");

  MCContext &Ctx = getParser().getContext();
  MCSymbol *ExitSym = Ctx.createTempSymbol("lthl_shift_exit", true);
  MCSymbol *LoopSym = Ctx.createTempSymbol("lthl_shift_loop", true);
  auto SymOp = [&](MCSymbol *Sym) {
    return MCOperand::createExpr(MCSymbolRefExpr::create(Sym, Ctx));
  };

  // Rd := Rs -- also the correct final answer on the amt-already-zero
  // fast path.
  emit(LTHL::ADD, {MCOperand::createReg(Rd), MCOperand::createReg(Rs),
                    MCOperand::createReg(LTHL::R0)});
  // r28 := 1 -- the loop's per-iteration decrement constant (see ADDI's
  // comment in LTHLInstrInfo.td: no reg-immediate ALU op exists, so
  // decrementing needs a real register holding 1).
  emit(LTHL::LD, {MCOperand::createReg(LTHL::R28), MCOperand::createImm(1)});
  // Test amt == 0 up front and skip the loop entirely if so -- same
  // SUB-into-r29-then-JZ shape ZERO_TEST_BR_PSEUDO's post-RA expansion
  // builds (see LTHLInstrInfo::expandPostRAPseudo).
  emit(LTHL::SUB, {MCOperand::createReg(LTHL::R29), MCOperand::createReg(Amt),
                    MCOperand::createReg(LTHL::R0)});
  emit(LTHL::JZ, {SymOp(ExitSym)});

  Out.emitLabel(LoopSym, IDLoc);
  emitBitOnce();
  emit(LTHL::SUB, {MCOperand::createReg(Amt), MCOperand::createReg(Amt),
                    MCOperand::createReg(LTHL::R28)});
  emit(LTHL::SUB, {MCOperand::createReg(LTHL::R29), MCOperand::createReg(Amt),
                    MCOperand::createReg(LTHL::R0)});
  emit(LTHL::JZ, {SymOp(ExitSym)});
  emit(LTHL::J, {SymOp(LoopSym)});

  Out.emitLabel(ExitSym, IDLoc);
  return false;
}

bool LTHLAsmParser::matchAndEmitInstruction(SMLoc Loc, unsigned &Opcode,
                                             OperandVector &Operands,
                                             MCStreamer &Out,
                                             uint64_t &ErrorInfo,
                                             bool MatchingInlineAsm) {
  // "call"/"shl"/"srl"/"sra" are hand-expanded pseudo-mnemonics, not
  // real LTHLInstrInfo.td instructions -- see expandCall's/expandShift's
  // comments.
  StringRef Mnemonic = static_cast<LTHLOperand &>(*Operands[0]).getToken();
  if (Mnemonic == "call")
    return expandCall(Operands, Loc, Out);
  if (Mnemonic == "shl")
    return expandShift(Operands, Loc, Out, LTHLShiftKind::SHL);
  if (Mnemonic == "srl")
    return expandShift(Operands, Loc, Out, LTHLShiftKind::SRL);
  if (Mnemonic == "sra")
    return expandShift(Operands, Loc, Out, LTHLShiftKind::SRA);

  MCInst Inst;
  unsigned MatchResult =
      MatchInstructionImpl(Operands, Inst, ErrorInfo, MatchingInlineAsm);

  switch (MatchResult) {
  case Match_Success:
    Inst.setLoc(Loc);
    Out.emitInstruction(Inst, *STI);
    return false;
  case Match_MnemonicFail:
    return Error(Loc, "invalid instruction mnemonic");
  case Match_InvalidOperand: {
    SMLoc ErrorLoc = Loc;
    if (ErrorInfo != ~0ULL) {
      if (ErrorInfo >= Operands.size())
        return Error(ErrorLoc, "too few operands for instruction");
      ErrorLoc = ((LTHLOperand &)*Operands[ErrorInfo]).getStartLoc();
      if (ErrorLoc == SMLoc())
        ErrorLoc = Loc;
    }
    return Error(ErrorLoc, "invalid operand for instruction");
  }
  default:
    return true;
  }
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeLTHLAsmParser() {
  RegisterMCAsmParser<LTHLAsmParser> X(getTheLTHLTarget());
}

#define GET_REGISTER_MATCHER
#define GET_MATCHER_IMPLEMENTATION
#include "LTHLGenAsmMatcher.inc"
