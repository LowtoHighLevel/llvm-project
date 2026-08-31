//===-- LTHLFrameLowering.h - LTHL Frame Information --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// LTHL has no dedicated frame pointer -- every function is purely
// SP-relative (see LTHLRegisterInfo::getFrameRegister()). This class
// describes that frame layout and emits the prologue/epilogue code that
// adjusts SP to make room for it.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_LTHL_LTHLFRAMELOWERING_H
#define LLVM_LIB_TARGET_LTHL_LTHLFRAMELOWERING_H

#include "llvm/CodeGen/TargetFrameLowering.h"

namespace llvm {

class LTHLSubtarget;
class TargetInstrInfo;

class LTHLFrameLowering : public TargetFrameLowering {
public:
  explicit LTHLFrameLowering(const LTHLSubtarget &STI);

  void emitPrologue(MachineFunction &MF,
                     MachineBasicBlock &MBB) const override;
  void emitEpilogue(MachineFunction &MF,
                     MachineBasicBlock &MBB) const override;

  MachineBasicBlock::iterator
  eliminateCallFramePseudoInstr(MachineFunction &MF, MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator MI) const override;

  void determineCalleeSaves(MachineFunction &MF, BitVector &SavedRegs,
                             RegScavenger *RS = nullptr) const override;

  static void adjustReg(const TargetInstrInfo &TII, MachineBasicBlock &MBB,
                         MachineBasicBlock::iterator MBBI, const DebugLoc &DL,
                         Register DestReg, Register SrcReg, int64_t Val,
                         Register ScratchReg);

protected:
  bool hasFPImpl(const MachineFunction &MF) const override;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_LTHL_LTHLFRAMELOWERING_H
