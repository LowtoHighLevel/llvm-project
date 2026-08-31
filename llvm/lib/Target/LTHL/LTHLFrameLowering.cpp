//===-- LTHLFrameLowering.cpp - LTHL Frame Information ------- ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// LTHL always uses SP-relative addressing (no frame pointer -- see
// hasFPImpl() below). The prologue/epilogue here just move SP by the
// frame size PEI has already computed in MachineFrameInfo; the harder
// part, turning individual stack-slot frame indices into real addresses,
// lives in LTHLRegisterInfo::eliminateFrameIndex and reuses adjustReg()
// from this file, since it's the exact same "materialize SP + offset"
// problem.
//
//===----------------------------------------------------------------------===//

#include "LTHLFrameLowering.h"
#include "LTHL.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;

// R30 = SP, per LTHLRegisterInfo::getFrameRegister(). Named here purely
// for readability at the call sites below.
static constexpr MCPhysReg SPReg = LTHL::R30;

LTHLFrameLowering::LTHLFrameLowering(const LTHLSubtarget &STI)
    : TargetFrameLowering(TargetFrameLowering::StackGrowsDown, Align(4),
                           /*LocalAreaOffset=*/0) {}

bool LTHLFrameLowering::hasFPImpl(const MachineFunction &MF) const {
  // LTHLRegisterInfo.td deliberately leaves R31/PC out of GPR and only
  // ever reserves R30/SP -- there's no separate frame-pointer register to
  // hand out, so every frame is SP-relative unconditionally.
  return false;
}

void LTHLFrameLowering::adjustReg(const TargetInstrInfo &TII,
                                   MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator MBBI,
                                   const DebugLoc &DL, Register DestReg,
                                   Register SrcReg, int64_t Val,
                                   Register ScratchReg) {
  if (Val == 0) {
    if (DestReg != SrcReg)
      BuildMI(MBB, MBBI, DL, TII.get(LTHL::ADD), DestReg)
          .addReg(SrcReg)
          .addReg(LTHL::R0); // R0 reads as zero -> DestReg = SrcReg + 0.
    return;
  }

  assert(isInt<24>(Val) &&
         "offset does not fit in LD's simm24 -- LTHL stack frames (and "
         "individual frame-index offsets) are limited to +/-2^23 bytes");

  // LD ScratchReg, Val ; ADD DestReg, SrcReg, ScratchReg
  BuildMI(MBB, MBBI, DL, TII.get(LTHL::LD), ScratchReg).addImm(Val);
  BuildMI(MBB, MBBI, DL, TII.get(LTHL::ADD), DestReg)
      .addReg(SrcReg)
      .addReg(ScratchReg);
}

void LTHLFrameLowering::determineCalleeSaves(MachineFunction &MF,
                                              BitVector &SavedRegs,
                                              RegScavenger *RS) const {
  // Handles CSR (R8-R14, LTHLCallingConv.td) exactly as before -- this
  // just adds R26 on top.
  TargetFrameLowering::determineCalleeSaves(MF, SavedRegs, RS);

  // r26 is the link register. It gets clobbered with the
  // callee's reutrn address for ret.
  if (MF.getFrameInfo().hasCalls())
    SavedRegs.set(LTHL::R26);
}

void LTHLFrameLowering::emitPrologue(MachineFunction &MF,
                                      MachineBasicBlock &MBB) const {
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MachineBasicBlock::iterator MBBI = MBB.begin();
  DebugLoc DL;

  uint64_t FrameSize = MFI.getStackSize();
  if (FrameSize == 0)
    return;

  // SP -= FrameSize. R29 is reserved globally (see
  // LTHLRegisterInfo::getReservedRegs()) precisely so it's always safe to
  // clobber here as scratch space.
  adjustReg(*TII, MBB, MBBI, DL, SPReg, SPReg,
            -static_cast<int64_t>(FrameSize), LTHL::R29);
}

void LTHLFrameLowering::emitEpilogue(MachineFunction &MF,
                                      MachineBasicBlock &MBB) const {
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  MachineBasicBlock::iterator MBBI = MBB.getFirstTerminator();
  DebugLoc DL = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();

  uint64_t FrameSize = MF.getFrameInfo().getStackSize();
  if (FrameSize == 0)
    return;

  // SP += FrameSize
  adjustReg(*TII, MBB, MBBI, DL, SPReg, SPReg,
            static_cast<int64_t>(FrameSize), LTHL::R29);
}

MachineBasicBlock::iterator LTHLFrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator MI) const {
  return MBB.erase(MI);
}
