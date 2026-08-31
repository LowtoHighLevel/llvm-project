//===-- LTHLMCInstLower.cpp - Convert LTHL MachineInstr to MCInst -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LTHLMCInstLower.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

MCSymbol *
LTHLMCInstLower::GetGlobalAddressSymbol(const MachineOperand &MO) const {
  // LTHL defines no MO_GlobalAddress target flags (LTHLII::MO_* doesn't
  // exist -- LD's immediate is a plain constant, see LTHLMCInstLower.h),
  // so the only legal value is the default of 0.
  assert(MO.getTargetFlags() == 0 && "Unknown target flag on GV operand");
  return Printer.getSymbol(MO.getGlobal());
}

MCSymbol *
LTHLMCInstLower::GetExternalSymbolSymbol(const MachineOperand &MO) const {
  assert(MO.getTargetFlags() == 0 && "Unknown target flag on GV operand");
  return Printer.GetExternalSymbolSymbol(MO.getSymbolName());
}

MCSymbol *
LTHLMCInstLower::GetBlockAddressSymbol(const MachineOperand &MO) const {
  assert(MO.getTargetFlags() == 0 && "Unknown target flag on GV operand");
  return Printer.GetBlockAddressSymbol(MO.getBlockAddress());
}

MCSymbol *LTHLMCInstLower::GetJumpTableSymbol(const MachineOperand &MO) const {
  const DataLayout &DL = Printer.getDataLayout();
  SmallString<256> Name;
  raw_svector_ostream(Name)
      << DL.getInternalSymbolPrefix() << "JTI" << Printer.getFunctionNumber()
      << '_' << MO.getIndex();
  return Ctx.getOrCreateSymbol(Name);
}

MCSymbol *
LTHLMCInstLower::GetConstantPoolIndexSymbol(const MachineOperand &MO) const {
  const DataLayout &DL = Printer.getDataLayout();
  SmallString<256> Name;
  raw_svector_ostream(Name)
      << DL.getInternalSymbolPrefix() << "CPI" << Printer.getFunctionNumber()
      << '_' << MO.getIndex();
  return Ctx.getOrCreateSymbol(Name);
}

MCOperand LTHLMCInstLower::LowerSymbolOperand(const MachineOperand &MO,
                                               MCSymbol *Sym) const {
  const MCExpr *Expr = MCSymbolRefExpr::create(Sym, Ctx);
  if (!MO.isJTI() && MO.getOffset())
    Expr = MCBinaryExpr::createAdd(
        Expr, MCConstantExpr::create(MO.getOffset(), Ctx), Ctx);
  return MCOperand::createExpr(Expr);
}

void LTHLMCInstLower::Lower(const MachineInstr *MI, MCInst &OutMI) const {
  OutMI.setOpcode(MI->getOpcode());

  for (const MachineOperand &MO : MI->operands()) {
    MCOperand MCOp;
    switch (MO.getType()) {
    default:
      MI->print(errs());
      llvm_unreachable("unknown operand type");
    case MachineOperand::MO_Register:
      // Ignore all implicit register operands -- this in particular
      // drops FLAGS (every ALU op's synthetic Def/Use scheduling edge,
      // never a real encoded operand) and the implicit argument-register
      // uses CALLJ picks up from CALL_PSEUDO's expansion (see
      // LTHLInstrInfo::expandPostRAPseudo) without needing any
      // LTHL-specific filtering here.
      if (MO.isImplicit())
        continue;
      MCOp = MCOperand::createReg(MO.getReg());
      break;
    case MachineOperand::MO_Immediate:
      MCOp = MCOperand::createImm(MO.getImm());
      break;
    case MachineOperand::MO_MachineBasicBlock:
      // J/JZ/JC/JV/JN's brtarget operand -- becomes a symbol-ref
      // expression that LTHLMCCodeEmitter::getBranchTargetOpValue routes
      // through fixup_lthl_pc16 (see that file's comment).
      MCOp = MCOperand::createExpr(
          MCSymbolRefExpr::create(MO.getMBB()->getSymbol(), Ctx));
      break;
    case MachineOperand::MO_GlobalAddress:
      MCOp = LowerSymbolOperand(MO, GetGlobalAddressSymbol(MO));
      break;
    case MachineOperand::MO_ExternalSymbol:
      MCOp = LowerSymbolOperand(MO, GetExternalSymbolSymbol(MO));
      break;
    case MachineOperand::MO_BlockAddress:
      MCOp = LowerSymbolOperand(MO, GetBlockAddressSymbol(MO));
      break;
    case MachineOperand::MO_JumpTableIndex:
      MCOp = LowerSymbolOperand(MO, GetJumpTableSymbol(MO));
      break;
    case MachineOperand::MO_ConstantPoolIndex:
      MCOp = LowerSymbolOperand(MO, GetConstantPoolIndexSymbol(MO));
      break;
    case MachineOperand::MO_RegisterMask:
      continue;
    }

    OutMI.addOperand(MCOp);
  }
}
