//===-- LTHLInstPrinter.h - LTHL Instruction Printer Header ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_LTHL_MCTARGETDESC_LTHLINSTPRINTER_H
#define LLVM_LIB_TARGET_LTHL_MCTARGETDESC_LTHLINSTPRINTER_H

#include "llvm/MC/MCInstPrinter.h"

namespace llvm {

class MCOperand;

class LTHLInstPrinter : public MCInstPrinter {
public:
  LTHLInstPrinter(const MCAsmInfo &MAI, const MCInstrInfo &MII,
                   const MCRegisterInfo &MRI)
      : MCInstPrinter(MAI, MII, MRI) {}

  void printInst(const MCInst *MI, uint64_t Address, StringRef Annot,
                  const MCSubtargetInfo &STI, raw_ostream &O) override;
  void printRegName(raw_ostream &O, MCRegister Reg) override;

  // 1. ADD THIS METHOD DECLARATION REQUIRED BY TABLEGEN
  std::pair<const char *, uint64_t> getMnemonic(const MCInst &MI) const override;

  bool printAliasInstr(const MCInst *MI, uint64_t Address, raw_ostream &OS);

  // Required by LTHLGenAsmWriter.inc's printAliasInstr whenever an
  // InstAlias operand needs a custom print method (here: the "mov"
  // alias for `add $rd, $rs, r0`). No alias operand in LTHL actually
  // uses a custom PrintMethod today, so this should never be reached --
  // llvm_unreachable is the tablegen-emitted default body.
  void printCustomAliasOperand(const MCInst *MI, uint64_t Address,
                                unsigned OpIdx, unsigned PrintMethodIdx,
                                raw_ostream &OS);

  void printInstruction(const MCInst *MI, uint64_t Address, raw_ostream &O);
  static const char *getRegisterName(MCRegister Reg);

  
private:
  void printOperand(const MCInst *MI, unsigned OpNo, raw_ostream &O);

  // Overload required by LTHLGenAsmWriter.inc for OPERAND_PCREL operands
  // (brtarget, used by J/JC/JN/JV/JZ) -- TableGen's AsmWriter emitter
  // threads the instruction's address through to the operand printer for
  // any operand of that OperandType, so it's available later to
  // compute/annotate the absolute target once a Disassembler exists.
  // No AsmBackend/Disassembler yet, so Address is unused for now.
  void printOperand(const MCInst *MI, uint64_t Address, unsigned OpNo,
                     raw_ostream &O);
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_LTHL_MCTARGETDESC_LTHLINSTPRINTER_H
