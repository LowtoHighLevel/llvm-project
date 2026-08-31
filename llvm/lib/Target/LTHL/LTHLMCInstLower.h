//===-- LTHLMCInstLower.h - Lower MachineInstr to MCInst ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// LTHLMCInstLower - lowers a MachineInstr into an MCInst, ready for
// LTHLAsmPrinter::emitInstruction to hand to the MCStreamer. Structural
// reference: MSP430MCInstLower.h / LanaiMCInstLower.h.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_LTHL_LTHLMCINSTLOWER_H
#define LLVM_LIB_TARGET_LTHL_LTHLMCINSTLOWER_H

#include "llvm/Support/Compiler.h"

namespace llvm {

class AsmPrinter;
class MCContext;
class MCInst;
class MCOperand;
class MCSymbol;
class MachineInstr;
class MachineOperand;

class LLVM_LIBRARY_VISIBILITY LTHLMCInstLower {
  MCContext &Ctx;
  AsmPrinter &Printer;

public:
  LTHLMCInstLower(MCContext &Ctx, AsmPrinter &Printer)
      : Ctx(Ctx), Printer(Printer) {}

  void Lower(const MachineInstr *MI, MCInst &OutMI) const;

  MCOperand LowerSymbolOperand(const MachineOperand &MO, MCSymbol *Sym) const;

  // GlobalAddress/ExternalSymbol: LTHLTargetLowering::LowerCall now lowers
  // both directly (via a hardcoded R28 scratch write -- see LowerCall's
  // own comment, and flag this as a spot to double check once R28 is
  // reserved/reworked), so these are reachable from ordinary codegen, not
  // just from an INLINEASM MachineInstr's operands (e.g.
  // `asm("ld $0, $1" : "=r"(x) : "i"(&g))`). BlockAddress is still only
  // reachable via inline asm today -- nothing in LTHLISelLowering.cpp
  // produces a BlockAddress SDNode yet.
  // JumpTableIndex/ConstantPoolIndex are implemented for the same
  // reason MSP430/Lanai always do: harmless boilerplate, ready for
  // whenever LTHLISelLowering grows switch/float-constant support,
  // neither of which exists yet.
  MCSymbol *GetGlobalAddressSymbol(const MachineOperand &MO) const;
  MCSymbol *GetExternalSymbolSymbol(const MachineOperand &MO) const;
  MCSymbol *GetBlockAddressSymbol(const MachineOperand &MO) const;
  MCSymbol *GetJumpTableSymbol(const MachineOperand &MO) const;
  MCSymbol *GetConstantPoolIndexSymbol(const MachineOperand &MO) const;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_LTHL_LTHLMCINSTLOWER_H
