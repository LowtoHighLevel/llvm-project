//===-- LTHLInstrInfo.h - LTHL Instruction Information -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_LTHL_LTHLINSTRINFO_H
#define LLVM_LIB_TARGET_LTHL_LTHLINSTRINFO_H

#include "LTHLRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"

#define GET_INSTRINFO_HEADER
#include "LTHLGenInstrInfo.inc"

namespace llvm {

class LTHLSubtarget;

// Condition-code values for the j<cc> (FormJRL/PC-relative) and jr<cc>
// (FormJRG/register-indirect) branch families -- one enum entry per
// *opcode*, not a generic immediate operand, since LTHLInstrInfo.td
// bakes each condition into its own instruction (JZ/JC/JV/JN, not one
// JCC taking a condition immediate). Values match arch-base.md's
// Conditions table (and JMPCondCode's Value field in LTHLInstrInfo.td)
// directly: 1=Zero, 2=Carry, 3=Overflow, 4=Neg. 0 (Absolute) isn't
// listed here since an unconditional branch is represented the usual
// LLVM way -- an empty Cond vector -- rather than as a condition value.
namespace LTHLCC {
enum CondCode { COND_Z = 1, COND_C = 2, COND_V = 3, COND_N = 4 };
} // namespace LTHLCC

class LTHLInstrInfo : public LTHLGenInstrInfo {
public:
  explicit LTHLInstrInfo(const LTHLSubtarget &STI);

  // LTHL has no dedicated move opcode -- `mov $rd, $rs` (see the
  // InstAlias in LTHLInstrInfo.td) is just `add $rd, $rs, r0`, so
  // that's what register-allocator-inserted copies expand to here.
  void copyPhysReg(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                    const DebugLoc &DL, Register DestReg, Register SrcReg,
                    bool KillSrc, bool RenamableDest = false,
                    bool RenamableSrc = false) const override;

  // LTHL has no base+offset addressing mode -- READ/WRITE take a plain
  // address *register*, not a frame-index-plus-offset memory operand --
  // so these just hand a FrameIndex operand straight to WRITE/READ and
  // let LTHLRegisterInfo::eliminateFrameIndex do the real work of turning
  // it into a concrete address (materializing SP + offset into R29 when
  // the offset is nonzero, exactly like every other frame-index use in
  // this backend).
  void storeRegToStackSlot(
      MachineBasicBlock &MBB, MachineBasicBlock::iterator MI, Register SrcReg,
      bool isKill, int FrameIndex, const TargetRegisterClass *RC,
      Register VReg,
      MachineInstr::MIFlag Flags = MachineInstr::NoFlags) const override;
  void loadRegFromStackSlot(
      MachineBasicBlock &MBB, MachineBasicBlock::iterator MI, Register DestReg,
      int FrameIdx, const TargetRegisterClass *RC, Register VReg,
      unsigned SubReg = 0,
      MachineInstr::MIFlag Flags = MachineInstr::NoFlags) const override;

  bool analyzeBranch(MachineBasicBlock &MBB, MachineBasicBlock *&TBB,
                      MachineBasicBlock *&FBB,
                      SmallVectorImpl<MachineOperand> &Cond,
                      bool AllowModify = false) const override;

  unsigned insertBranch(MachineBasicBlock &MBB, MachineBasicBlock *TBB,
                         MachineBasicBlock *FBB,
                         ArrayRef<MachineOperand> Cond, const DebugLoc &DL,
                         int *BytesAdded = nullptr) const override;

  unsigned removeBranch(MachineBasicBlock &MBB,
                         int *BytesRemoved = nullptr) const override;


  // Expands CALL_PSEUDO into its real instruction sequence. Deliberately
  // NOT done earlier via usesCustomInserter -- see CALL_PSEUDO's comment
  // in LTHLInstrInfo.td for why this specific expansion has to survive
  // scheduling and register allocation untouched.
  bool expandPostRAPseudo(MachineInstr &MI) const override;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_LTHL_LTHLINSTRINFO_H
