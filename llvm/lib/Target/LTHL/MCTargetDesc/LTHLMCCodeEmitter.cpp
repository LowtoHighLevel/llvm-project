//===-- LTHLMCCodeEmitter.cpp - Convert LTHL code to machine code -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Emits the 4-byte binary encoding for every concrete (non-pseudo) LTHL
// instruction.
//
//===----------------------------------------------------------------------===//

#include "LTHL.h"
#include "MCTargetDesc/LTHLFixupKinds.h"
#include "MCTargetDesc/LTHLMCCodeEmitter.h"
#include "MCTargetDesc/LTHLMCTargetDesc.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h" // isInt<N>

#define DEBUG_TYPE "mccodeemitter"

using namespace llvm;

void LTHLMCCodeEmitter::encodeInstruction(const MCInst &MI,
                                           SmallVectorImpl<char> &CB,
                                           SmallVectorImpl<MCFixup> &Fixups,
                                           const MCSubtargetInfo &STI) const {
  const MCInstrDesc &Desc = MCII.get(MI.getOpcode());
  (void)Desc;
  assert(Desc.getSize() == 4 &&
         "every concrete LTHLInst is defined with Size = 4 -- a "
         "non-4-byte Desc here means a pseudo reached the emitter, or a "
         "new instruction was added without going through LTHLInst");

  uint64_t Bits = getBinaryCodeForInstr(MI, Fixups, STI);
  // See this file's header comment -- byte order is an assumption, not
  // yet confirmed against the doc or real hardware.
  support::endian::write<uint32_t>(CB, (uint32_t)Bits,
                                    llvm::endianness::little);
}

unsigned
LTHLMCCodeEmitter::getMachineOpValue(const MCInst &MI, const MCOperand &MO,
                                      SmallVectorImpl<MCFixup> &Fixups,
                                      const MCSubtargetInfo &STI) const {
  if (MO.isReg())
    // Register N encodes as the literal value N (0-31) -- see
    // LTHLRegisterInfo.td's LTHLReg: `let HWEncoding{15-0} = enc;` with
    // enc running 0-31 via the `foreach I = 0-31` register defs.
    return Ctx.getRegisterInfo()->getEncodingValue(MO.getReg());

  if (MO.isImm()) {
    // NOTE: this used to assume field width (5 bits for a register slot,
    // 24 for simm24, ...) was enforced by the generated
    // getBinaryCodeForInstr when it shifts/ORs this value into the Inst
    // word. It isn't -- TableGen's default CodeEmitterGen output trusts
    // the value handed to it is already correctly sized and does not
    // mask to the field's declared width before OR-ing it in. An
    // out-of-range simm24 operand (e.g. a 32-bit constant that should
    // have been routed through LTHLISelDAGToDAG.cpp's selectConstant()
    // Horner's-method expansion but wasn't, for whatever reason) does
    // not get silently truncated -- its high bits bleed directly into
    // LD's adjacent reg/opcode fields in the same instruction word,
    // corrupting or completely changing the encoded instruction. This
    // assert is a last-line-of-defense catch for that, independent of
    // whatever's supposed to prevent it upstream in ISel.
    int64_t Imm = MO.getImm();
    assert(isInt<24>(Imm) &&
           "LTHLMCCodeEmitter: immediate operand does not fit LD's 24-bit "
           "signed field -- this should have been routed through "
           "LTHLISelDAGToDAG.cpp's selectConstant() Horner's-method "
           "expansion before reaching the emitter. Encoding it directly "
           "would corrupt adjacent bit-fields (reg/opcode) in the same "
           "instruction word, not just truncate silently.");
    return (unsigned)Imm;
  }

  // The only operand type that reaches here with a symbol expression
  // today is LD's simm24 (see LTHLInstrInfo.td's simm24 def -- it has a
  // DecoderMethod but deliberately no EncoderMethod, so it's routed
  // through this default path rather than a dedicated getXxxOpValue).
  // LTHLAsmParser.cpp's addImmOperands comment calls this exact gap out:
  // "there's no MCCodeEmitter yet to actually resolve/encode it
  // against" -- this closes it, modulo LTHLAsmBackend still not
  // existing to apply the fixup (see this file's header comment).
  assert(MO.isExpr() &&
         "operand is neither a register, an immediate, nor an expression");
  Fixups.push_back(MCFixup::create(0, MO.getExpr(),
                                    MCFixupKind(LTHL::fixup_lthl_imm24),
                                    /*PCRel=*/false));
  return 0;
}

uint64_t LTHLMCCodeEmitter::getBranchTargetOpValue(
    const MCInst &MI, unsigned OpNo, SmallVectorImpl<MCFixup> &Fixups,
    const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);

  // A resolved numeric literal (e.g. hand-written `j .+8`-style asm with
  // an already-constant expression) -- rare in practice, since
  // AsmPrinter normally lowers a branch's target MachineBasicBlock into
  // an MCSymbolRefExpr, which always takes the fixup path below instead.
  if (MO.isImm())
    return (uint64_t)MO.getImm();

  assert(MO.isExpr() && "brtarget operand must be a resolved immediate or "
                         "an expression");
  // See LTHLFixupKinds.h's fixup_lthl_pc16 comment for the "relative to
  // this instruction's own address, not PC+4" assumption this bakes in.
  Fixups.push_back(MCFixup::create(0, MO.getExpr(),
                                    MCFixupKind(LTHL::fixup_lthl_pc16),
                                    /*PCRel=*/true));
  return 0;
}

uint64_t LTHLMCCodeEmitter::getCallTargetOpValue(
    const MCInst &MI, unsigned OpNo, SmallVectorImpl<MCFixup> &Fixups,
    const MCSubtargetInfo &STI) const {
  // See LTHLMCCodeEmitter.h and LTHLFixupKinds.h -- `calltarget` isn't
  // attached to any concrete instruction yet, so this is currently dead
  // code from LTHLGenMCCodeEmitter.inc's point of view. Implemented with
  // the same shape as getBranchTargetOpValue above so it's ready the
  // moment that changes.
  const MCOperand &MO = MI.getOperand(OpNo);

  if (MO.isImm())
    return (uint64_t)MO.getImm();

  assert(MO.isExpr() && "calltarget operand must be a resolved immediate "
                         "or an expression");
  Fixups.push_back(MCFixup::create(0, MO.getExpr(),
                                    MCFixupKind(LTHL::fixup_lthl_calltarget),
                                    /*PCRel=*/false));
  return 0;
}

MCCodeEmitter *llvm::createLTHLMCCodeEmitter(const MCInstrInfo &MCII,
                                              MCContext &Ctx) {
  return new LTHLMCCodeEmitter(Ctx, MCII);
}

#include "LTHLGenMCCodeEmitter.inc"
