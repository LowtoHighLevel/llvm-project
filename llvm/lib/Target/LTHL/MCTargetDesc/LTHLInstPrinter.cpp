//===-- LTHLInstPrinter.cpp - Convert MCInst to asm syntax -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LTHLInstPrinter.h"
#include "MCTargetDesc/LTHLMCTargetDesc.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "asm-printer"

// Pulls in the generated printInstruction(), getRegisterName(), and
// operand-name tables built from LTHLInstrInfo.td's AsmStrings.
#define GET_INSTRUCTION_NAME
#define PRINT_ALIAS_INSTR
#include "LTHLGenAsmWriter.inc"

void LTHLInstPrinter::printInst(const MCInst *MI, uint64_t Address,
                                 StringRef Annot, const MCSubtargetInfo &STI,
                                 raw_ostream &O) {
  printInstruction(MI, Address, O);
  printAnnotation(O, Annot);
}

void LTHLInstPrinter::printRegName(raw_ostream &O, MCRegister Reg) {
  O << getRegisterName(Reg);
}

void LTHLInstPrinter::printOperand(const MCInst *MI, unsigned OpNo,
                                    raw_ostream &O) {
  const MCOperand &Op = MI->getOperand(OpNo);

  if (Op.isReg()) {
    printRegName(O, Op.getReg());
    return;
  }

  if (Op.isImm()) {
    O << Op.getImm();
    return;
  }

  // Anything else (branch/call targets not yet resolved to an immediate)
  // is a symbolic MCExpr — e.g. a basic block or function label.
  assert(Op.isExpr() && "unknown operand kind in printOperand");
  MAI.printExpr(O, *Op.getExpr());
}

// PC-relative overload (see the header comment): brtarget (J/JC/JN/JV/JZ)
// is OPERAND_PCREL, so LTHLGenAsmWriter.inc calls this 4-arg form instead
// of the plain one above. There's no Disassembler yet, so there's nothing
// useful to compute from Address (e.g. an absolute-target comment) yet —
// just fall back to the ordinary operand printer.
void LTHLInstPrinter::printOperand(const MCInst *MI, uint64_t Address,
                                    unsigned OpNo, raw_ostream &O) {
  printOperand(MI, OpNo, O);
}
