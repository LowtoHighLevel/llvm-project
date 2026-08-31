//===-- LTHLRegisterInfo.h - LTHL Register Information ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Declares LTHL's TargetRegisterInfo subclass: which physical registers
// exist to the register allocator, which are reserved (never allocated),
// callee-saved lists, and how stack-slot frame indices get turned into
// real SP-relative addresses.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_LTHL_LTHLREGISTERINFO_H
#define LLVM_LIB_TARGET_LTHL_LTHLREGISTERINFO_H

#include "llvm/CodeGen/TargetRegisterInfo.h"

#define GET_REGINFO_HEADER
#include "LTHLGenRegisterInfo.inc"

namespace llvm {

class TargetInstrInfo;

struct LTHLRegisterInfo : public LTHLGenRegisterInfo {
  LTHLRegisterInfo();

  const MCPhysReg *
  getCalleeSavedRegs(const MachineFunction *MF) const override;

  BitVector getReservedRegs(const MachineFunction &MF) const override;

  // Used by LTHLISelLowering::LowerCall to mark registers a callee is
  // free to clobber. Generated from the same `def CSR : CalleeSavedRegs`
  // in LTHLCallingConv.td that getCalleeSavedRegs() reads.
  const uint32_t *getCallPreservedMask(const MachineFunction &MF,
                                        CallingConv::ID) const override;

  // LTHL has no base+offset addressing mode, so this also emits the
  // LD+ADD sequence (via LTHLFrameLowering::adjustReg) needed to
  // materialize a stack slot's address into R29 when its offset isn't 0.
  bool eliminateFrameIndex(MachineBasicBlock::iterator MI, int SPAdj,
                            unsigned FIOperandNum,
                            RegScavenger *RS = nullptr) const override;

  Register getFrameRegister(const MachineFunction &MF) const override;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_LTHL_LTHLREGISTERINFO_H
