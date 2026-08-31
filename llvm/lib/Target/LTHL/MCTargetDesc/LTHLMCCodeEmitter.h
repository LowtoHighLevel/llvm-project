//===-- LTHLMCCodeEmitter.h - Convert LTHL code to machine code --*- C++ -*-===//
//
// Every real (non-pseudo) LTHL instruction is exactly 4 bytes (see
// LTHLInstrInfo.td's LTHLInst base class -- `let Size = 4;` is
// unconditional), so unlike a variable-length ISA's emitter this one
// never needs to track a running byte offset across multiple operands
// within a single instruction -- getBinaryCodeForInstr (TableGen-
// generated, from LTHLGenMCCodeEmitter.inc) packs the whole 32-bit word
// in one call, and encodeInstruction below just writes it out.
//
// LTHLPseudo-based instructions (CALL_PSEUDO, ADDRFI, ADDI,
// BR_CC_PSEUDO) never reach this class at all -- they're always fully
// expanded (via usesCustomInserter or expandPostRAPseudo) before
// AsmPrinter/MCCodeEmitter ever sees a MachineFunction, so
// getBinaryCodeForInstr only ever needs to handle the concrete Form*-
// based defs.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_LTHL_MCTARGETDESC_LTHLMCCODEEMITTER_H
#define LLVM_LIB_TARGET_LTHL_MCTARGETDESC_LTHLMCCODEEMITTER_H

#include "llvm/MC/MCCodeEmitter.h"

namespace llvm {

class MCContext;
class MCInst;
class MCInstrInfo;
class MCFixup;
class MCOperand;
class MCSubtargetInfo;
template <typename T> class SmallVectorImpl;

class LTHLMCCodeEmitter : public MCCodeEmitter {
  MCContext &Ctx;
  const MCInstrInfo &MCII;

public:
  LTHLMCCodeEmitter(MCContext &Ctx, const MCInstrInfo &MCII)
      : Ctx(Ctx), MCII(MCII) {}
  LTHLMCCodeEmitter(const LTHLMCCodeEmitter &) = delete;
  LTHLMCCodeEmitter &operator=(const LTHLMCCodeEmitter &) = delete;
  ~LTHLMCCodeEmitter() override = default;

  void encodeInstruction(const MCInst &MI, SmallVectorImpl<char> &CB,
                          SmallVectorImpl<MCFixup> &Fixups,
                          const MCSubtargetInfo &STI) const override;

  /// TableGen'erated function (LTHLGenMCCodeEmitter.inc) that packs a
  /// whole instruction's operands into its 32-bit Inst value, calling
  /// back into getMachineOpValue/getBranchTargetOpValue/
  /// getCallTargetOpValue below wherever LTHLInstrInfo.td declares a
  /// custom EncoderMethod (or the default, for everything else).
  uint64_t getBinaryCodeForInstr(const MCInst &MI,
                                  SmallVectorImpl<MCFixup> &Fixups,
                                  const MCSubtargetInfo &STI) const;

  /// Default operand encoder -- handles every plain GPR register operand
  /// (rd/rs1/rs2/addr/data/target/reg, across all Form* classes) and
  /// LD's simm24 immediate. Registers resolve via HWEncoding (see
  /// LTHLRegisterInfo.td -- register N encodes as the literal value N,
  /// 0-31); immediates return their constant value directly; a symbol
  /// expression reaching a plain (non-EncoderMethod) operand -- today,
  /// only possible via LD's simm24 -- raises fixup_lthl_imm24, closing
  /// the gap LTHLAsmParser.cpp's SImm24AsmOperand comment flags.
  unsigned getMachineOpValue(const MCInst &MI, const MCOperand &MO,
                              SmallVectorImpl<MCFixup> &Fixups,
                              const MCSubtargetInfo &STI) const;

  /// EncoderMethod for `brtarget` (J/JZ/JC/JV/JN's PC-relative $rel
  /// operand, FormJRL). A resolved compile-time-constant offset (rare in
  /// practice -- AsmPrinter lowers a MachineOperand::MBB into an
  /// MCSymbolRefExpr, so this path is really only reachable for
  /// hand-written asm with a numeric literal target) is returned as-is;
  /// everything else raises fixup_lthl_pc16 -- see LTHLFixupKinds.h for
  /// the PC-relative-origin assumption this bakes in.
  uint64_t getBranchTargetOpValue(const MCInst &MI, unsigned OpNo,
                                   SmallVectorImpl<MCFixup> &Fixups,
                                   const MCSubtargetInfo &STI) const;

  /// EncoderMethod for `calltarget` -- see LTHLFixupKinds.h's
  /// fixup_lthl_calltarget: this operand class isn't attached to any
  /// concrete instruction yet, so LTHLGenMCCodeEmitter.inc won't
  /// actually call this today. Implemented now anyway so the
  /// EncoderMethod string in LTHLInstrInfo.td has a real definition to
  /// resolve against the moment it *is* wired up.
  uint64_t getCallTargetOpValue(const MCInst &MI, unsigned OpNo,
                                 SmallVectorImpl<MCFixup> &Fixups,
                                 const MCSubtargetInfo &STI) const;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_LTHL_MCTARGETDESC_LTHLMCCODEEMITTER_H
