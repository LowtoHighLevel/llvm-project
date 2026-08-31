//===-- LTHLRegisterInfo.cpp - LTHL Register Information ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LTHLRegisterInfo.h"
#include "LTHL.h"
#include "LTHLFrameLowering.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"

#define GET_REGINFO_TARGET_DESC
#include "LTHLGenRegisterInfo.inc"

using namespace llvm;

// R26 is the DWARF return-address register number — matches the value
// passed to InitLTHLMCRegisterInfo() in the MC layer; keep these two in
// sync if this ever changes.
LTHLRegisterInfo::LTHLRegisterInfo() : LTHLGenRegisterInfo(LTHL::R26) {}

const MCPhysReg *
LTHLRegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  return CSR_SaveList;
}

const uint32_t *
LTHLRegisterInfo::getCallPreservedMask(const MachineFunction &MF,
                                        CallingConv::ID) const {
  return CSR_RegMask;
}

BitVector LTHLRegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  BitVector Reserved(getNumRegs());

  // R0 = hardwired zero register
  Reserved.set(LTHL::R0);

  // R30 = stack pointer. (by convention)
  Reserved.set(LTHL::R30);

  // R31 = program counter.
  Reserved.set(LTHL::R31);

  // R29 = fixed scratch register for address materialization. LTHL has
  // no register+immediate add and no base+offset addressing mode, so
  // LTHLFrameLowering::adjustReg (used for SP adjustment in the
  // prologue/epilogue, and for stack-slot address materialization in
  // eliminateFrameIndex below) needs a register it can clobber
  // unconditionally, at arbitrary points, without going through the
  // register allocator or a RegScavenger. Reserving it here is what
  // makes that safe.
  Reserved.set(LTHL::R29);

  // R26 = link register (by convention)
  Reserved.set(LTHL::R26);

  return Reserved;
}

bool LTHLRegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator MI,
                                            int SPAdj, unsigned FIOperandNum,
                                            RegScavenger *RS) const {
  assert(SPAdj == 0 && "SPAdj should stay 0 -- LTHLFrameLowering reserves "
                        "the call frame (hasReservedCallFrame), so there "
                        "are no ADJCALLSTACK pseudos moving SP mid-block");

  MachineInstr &Instr = *MI;
  MachineBasicBlock &MBB = *Instr.getParent();
  MachineFunction &MF = *MBB.getParent();
  const TargetFrameLowering *TFI = MF.getSubtarget().getFrameLowering();
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  DebugLoc DL = Instr.getDebugLoc();

  int FrameIndex = Instr.getOperand(FIOperandNum).getIndex();
  Register FrameReg;
  int64_t Offset =
      TFI->getFrameIndexReference(MF, FrameIndex, FrameReg).getFixed();

  if (Offset == 0) {
    // The slot's address is exactly the frame register -- no extra
    // instructions needed, just point the operand at it directly.
    Instr.getOperand(FIOperandNum).ChangeToRegister(FrameReg, false);
    return false;
  }

  // LTHL's load/store instructions have no base+offset addressing mode
  // (see LTHLInstrInfo.td), so the effective address has to be computed
  // into a register before Instr runs. R29 is reserved globally (see
  // getReservedRegs() above) specifically so it's always safe to clobber
  // right here for exactly this purpose.
  LTHLFrameLowering::adjustReg(*TII, MBB, MI, DL, LTHL::R29, FrameReg,
                                Offset, LTHL::R29);
  Instr.getOperand(FIOperandNum).ChangeToRegister(LTHL::R29, false);
  return false;
}

Register LTHLRegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  // No dedicated frame pointer -- everything is SP-relative, so the
  // "frame register" LLVM asks about is just the stack pointer.
  return LTHL::R30;
}
