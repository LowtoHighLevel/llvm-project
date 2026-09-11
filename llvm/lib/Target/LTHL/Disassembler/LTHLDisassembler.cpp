//===-- LTHLDisassembler.cpp - Disassembler for LTHL ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Disassembler for LTHL: turns a stream of little-endian 32-bit words back
// into MCInsts. Every concrete (non-pseudo) instruction defined via
// LTHLInst in LTHLInstrInfo.td is exactly 4 bytes (`let Size = 4`), so this
// is about as simple as -gen-disassembler gets: fixed width, no
// addressing-mode dispatch the way MSP430/Lanai need for their variable
// instruction lengths.
//===----------------------------------------------------------------------===//

#include "LTHLDisassembler.h"
#include "MCTargetDesc/LTHLMCTargetDesc.h"
#include "TargetInfo/LTHLTargetInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDecoder.h"
#include "llvm/MC/MCDecoderOps.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/MathExtras.h"

#define DEBUG_TYPE "lthl-disassembler"

using namespace llvm;
using namespace llvm::MCD;

typedef MCDisassembler::DecodeStatus DecodeStatus;

static MCDisassembler *createLTHLDisassembler(const Target & /*T*/,
                                               const MCSubtargetInfo &STI,
                                               MCContext &Ctx) {
  return new LTHLDisassembler(STI, Ctx);
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeLTHLDisassembler() {
  TargetRegistry::RegisterMCDisassembler(getTheLTHLTarget(),
                                          createLTHLDisassembler);
}
static const unsigned GPRDecoderTable[] = {
  LTHL::R0,  LTHL::R1,  LTHL::R2,  LTHL::R3,  LTHL::R4,  LTHL::R5,
  LTHL::R6,  LTHL::R7,  LTHL::R8,  LTHL::R9,  LTHL::R10, LTHL::R11,
  LTHL::R12, LTHL::R13, LTHL::R14, LTHL::R15, LTHL::R16, LTHL::R17,
  LTHL::R18, LTHL::R19, LTHL::R20, LTHL::R21, LTHL::R22, LTHL::R23,
  LTHL::R24, LTHL::R25, LTHL::R26, LTHL::R27, LTHL::R28, LTHL::R29,
  LTHL::R30
};
// clang-format on

static DecodeStatus
DecodeGPRRegisterClass(MCInst &Inst, uint64_t RegNo, uint64_t /*Address*/,
                        const MCDisassembler * /*Decoder*/) {
  // R31 (PC) has no GPR class member -- see this file's header comment
  // (item 2) for why a real ADD encoding can never actually produce
  // RegNo==31 here in practice; this Fail is defensive, not reachable
  // from real object code.
  if (RegNo > 30)
    return MCDisassembler::Fail;

  Inst.addOperand(MCOperand::createReg(GPRDecoderTable[RegNo]));
  return MCDisassembler::Success;
}

// LD's simm24 (and simm16, defined for future use but not yet attached to
// any concrete instruction) immediate: a plain sign-extended field, no
// relocation/symbolization involved -- LD's immediate is either a
// compile-time constant, or, for a materialized global/external-symbol
// address, a value that's already been fully resolved by the linker by
// the time anything is disassembling finished object code (the fixup --
// see LTHLFixupKinds.h's fixup_lthl_imm24 -- only exists transiently
// between MCCodeEmitter and the object writer). Matches simm24/simm16's
// `DecoderMethod = "decodeSImmOperand<N>"` in LTHLInstrInfo.td.
template <unsigned N>
static DecodeStatus decodeSImmOperand(MCInst &Inst, uint64_t Imm,
                                       uint64_t /*Address*/,
                                       const MCDisassembler * /*Decoder*/) {
  Inst.addOperand(MCOperand::createImm(SignExtend64<N>(Imm)));
  return MCDisassembler::Success;
}

// brtarget (J/JZ/JC/JV/JN's `rel` field, FormJRL): a 16-bit signed offset
// relative to the branch instruction's own address (SHORT-REL -- see
// LTHLFixupKinds.h's fixup_lthl_pc16 comment and the backend notes for
// how that "own address, not PC+4" convention was confirmed against the
// emulator). Tries to resolve to a symbol first (useful under
// `llvm-objdump -d` when symbol information is available), and falls
// back to the raw signed offset otherwise -- which is exactly what
// LTHLInstPrinter::printOperand(MI, Address, OpNo, O) prints as-is today,
// and exactly what LTHLAsmParser/LTHLMCCodeEmitter round-trip through for
// hand-written `j <label>` asm, so a disassembled-then-reassembled
// instruction stream stays bit-for-bit identical either way.
static DecodeStatus decodeBranchTarget(MCInst &Inst, uint64_t Imm,
                                        uint64_t Address,
                                        const MCDisassembler *Decoder) {
  int64_t Offset = SignExtend64<16>(Imm);
  if (!Decoder->tryAddingSymbolicOperand(Inst, Address + Offset, Address,
                                          /*IsBranch=*/true, /*Offset=*/0,
                                          /*OpSize=*/2, /*InstSize=*/4))
    Inst.addOperand(MCOperand::createImm(Offset));
  return MCDisassembler::Success;
}

#include "LTHLGenDisassemblerTables.inc"

DecodeStatus LTHLDisassembler::getInstruction(MCInst &Instr, uint64_t &Size,
                                               ArrayRef<uint8_t> Bytes,
                                               uint64_t Address,
                                               raw_ostream & /*CStream*/) const {
  if (Bytes.size() < 4) {
    Size = 0;
    return MCDisassembler::Fail;
  }

  // Little-endian 32-bit word -- matches
  // LTHLMCCodeEmitter::encodeInstruction's
  // support::endian::write<uint32_t>(..., llvm::endianness::little).
  uint32_t Insn = support::endian::read32le(Bytes.data());

  DecodeStatus Result =
      decodeInstruction(DecoderTable32, Instr, Insn, Address, this, STI);

  if (Result != MCDisassembler::Fail) {
    Size = 4;
    return Result;
  }

  // Nothing matched: an undefined encoding (EXT1/EXT2's opcode bits, a
  // reserved WIDTH value of 0b11 on READ/WRITE, an out-of-range
  // JMPREL/JMPREG condition field, ...). Skip one word and let the
  // caller keep going -- same convention MSP430/Lanai use for an
  // unrecognized instruction.
  Size = 4;
  return MCDisassembler::Fail;
}
