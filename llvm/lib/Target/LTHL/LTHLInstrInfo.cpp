//===-- LTHLInstrInfo.cpp - LTHL Instruction Information ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LTHLInstrInfo.h"
#include "LTHL.h"
#include "LTHLSubtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/Support/ErrorHandling.h"

#define GET_INSTRINFO_CTOR_DTOR
#include "LTHLGenInstrInfo.inc"

using namespace llvm;

LTHLInstrInfo::LTHLInstrInfo(const LTHLSubtarget &STI)
    : LTHLGenInstrInfo(STI, *STI.getRegisterInfo(), LTHL::ADJCALLSTACKDOWN,
                        LTHL::ADJCALLSTACKUP) {}

void LTHLInstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator I,
                                 const DebugLoc &DL, Register DestReg,
                                 Register SrcReg, bool KillSrc,
                                 bool RenamableDest, bool RenamableSrc) const {
  // LTHL only has the one register class and no dedicated move opcode --
  // `mov $rd, $rs` (see the InstAlias in LTHLInstrInfo.td) is just
  // `add $rd, $rs, r0` (r0 always reads as zero), so that's exactly what
  // gets built here. LTHLInstPrinter is built with PRINT_ALIAS_INSTR, so
  // once asm printing exists this will render back out as "mov" text.
  assert(LTHL::GPRRegClass.contains(DestReg, SrcReg) &&
         "Impossible reg-to-reg copy");
  BuildMI(MBB, I, DL, get(LTHL::ADD), DestReg)
      .addReg(SrcReg, getKillRegState(KillSrc))
      .addReg(LTHL::R0);
}

void LTHLInstrInfo::storeRegToStackSlot(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI, Register SrcReg,
    bool isKill, int FrameIndex, const TargetRegisterClass *RC,
    Register VReg, MachineInstr::MIFlag Flags) const {
  DebugLoc DL;
  if (MI != MBB.end())
    DL = MI->getDebugLoc();
  MachineFunction &MF = *MBB.getParent();
  MachineFrameInfo &MFI = MF.getFrameInfo();

  assert(RC == &LTHL::GPRRegClass &&
         "LTHL only has the one register class -- unexpected spill class");

  MachineMemOperand *MMO = MF.getMachineMemOperand(
      MachinePointerInfo::getFixedStack(MF, FrameIndex),
      MachineMemOperand::MOStore, MFI.getObjectSize(FrameIndex),
      MFI.getObjectAlign(FrameIndex));

  // WRITE $addr, $data -- $addr takes the FrameIndex operand directly.
  // LTHLRegisterInfo::eliminateFrameIndex resolves it later (to the frame
  // register alone when the offset is zero, or to R29 holding a
  // materialized SP+offset address otherwise) -- same as every other
  // frame-index use in this backend; nothing special is needed here.
  BuildMI(MBB, MI, DL, get(LTHL::WRITE))
      .addFrameIndex(FrameIndex)
      .addReg(SrcReg, getKillRegState(isKill))
      .addMemOperand(MMO)
      .setMIFlag(Flags);
}

void LTHLInstrInfo::loadRegFromStackSlot(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI, Register DestReg,
    int FrameIdx, const TargetRegisterClass *RC, Register VReg,
    unsigned SubReg, MachineInstr::MIFlag Flags) const {
  DebugLoc DL;
  if (MI != MBB.end())
    DL = MI->getDebugLoc();
  MachineFunction &MF = *MBB.getParent();
  MachineFrameInfo &MFI = MF.getFrameInfo();

  assert(RC == &LTHL::GPRRegClass &&
         "LTHL only has the one register class -- unexpected reload class");
  assert(SubReg == 0 && "LTHL registers have no subregisters");

  MachineMemOperand *MMO = MF.getMachineMemOperand(
      MachinePointerInfo::getFixedStack(MF, FrameIdx),
      MachineMemOperand::MOLoad, MFI.getObjectSize(FrameIdx),
      MFI.getObjectAlign(FrameIdx));

  // READ $addr, $data -- same FrameIndex-operand handoff as above, just
  // on the load side; $data is the outs operand, so it's DestReg here.
  BuildMI(MBB, MI, DL, get(LTHL::READ), DestReg)
      .addFrameIndex(FrameIdx)
      .addMemOperand(MMO)
      .setMIFlag(Flags);
}

static unsigned getCondBranchOpcode(int64_t CC) {
  switch (CC) {
  case LTHLCC::COND_Z:
    return LTHL::JZ;
  case LTHLCC::COND_C:
    return LTHL::JC;
  case LTHLCC::COND_V:
    return LTHL::JV;
  case LTHLCC::COND_N:
    return LTHL::JN;
  }
  llvm_unreachable("Invalid LTHL branch condition");
}

// The LTHLCC::CondCode a J* opcode tests, or -1 if Opc isn't one of the
// conditional relative-branch opcodes. J itself is unconditional and
// handled separately by its callers; the jr<cc> register-indirect
// family isn't part of this mapping at all (analyzeBranch bails out on
// any indirect branch before ever calling this).
static int getCondFromBranchOpc(unsigned Opc) {
  switch (Opc) {
  case LTHL::JZ:
    return LTHLCC::COND_Z;
  case LTHL::JC:
    return LTHLCC::COND_C;
  case LTHL::JV:
    return LTHLCC::COND_V;
  case LTHL::JN:
    return LTHLCC::COND_N;
  default:
    return -1;
  }
}

bool LTHLInstrInfo::analyzeBranch(MachineBasicBlock &MBB,
                                   MachineBasicBlock *&TBB,
                                   MachineBasicBlock *&FBB,
                                   SmallVectorImpl<MachineOperand> &Cond,
                                   bool AllowModify) const {
  // Standard from-the-bottom terminator walk (see e.g.
  // MSP430InstrInfo::analyzeBranch for the same shape), adapted for
  // LTHL's one-opcode-per-condition encoding: J is unconditional,
  // JZ/JC/JV/JN each test exactly one flag, and there's no single JCC
  // form taking a condition immediate.
  MachineBasicBlock::iterator I = MBB.end();
  while (I != MBB.begin()) {
    --I;
    if (I->isDebugInstr())
      continue;

    // Once we hit a non-terminator, everything above it is real code --
    // we're done, successfully.
    if (!isUnpredicatedTerminator(*I))
      break;

    // A terminator that isn't a branch (e.g. RET) can't be analyzed by
    // this routine at all.
    if (!I->isBranch())
      return true;

    // JR/JRcc (brind/switch lowering) -- leave alone.
    if (I->isIndirectBranch())
      return true;

    if (I->getOpcode() == LTHL::J) {
      if (!AllowModify) {
        TBB = I->getOperand(0).getMBB();
        continue;
      }

      // If the block has any instructions after an unconditional J,
      // they're unreachable -- delete them.
      MBB.erase(std::next(I), MBB.end());
      Cond.clear();
      FBB = nullptr;

      // Delete the J if it's equivalent to a fall-through.
      if (MBB.isLayoutSuccessor(I->getOperand(0).getMBB())) {
        TBB = nullptr;
        I->eraseFromParent();
        I = MBB.end();
        continue;
      }

      TBB = I->getOperand(0).getMBB();
      continue;
    }

    int CC = getCondFromBranchOpc(I->getOpcode());
    if (CC < 0)
      return true; // Not a branch opcode we recognize.

    // Working from the bottom, handle the first conditional branch.
    if (Cond.empty()) {
      FBB = TBB;
      TBB = I->getOperand(0).getMBB();
      Cond.push_back(MachineOperand::CreateImm(CC));
      continue;
    }

    // Only handle the case where all conditional branches in the block
    // target the same destination with the same condition -- anything
    // else (and in particular, there is no way to fold two different
    // single-flag conditions into one Cond the way a real multi-bit
    // condition-code ISA could) isn't analyzable here.
    assert(Cond.size() == 1);
    assert(TBB);

    if (TBB != I->getOperand(0).getMBB())
      return true;

    if (Cond[0].getImm() == CC)
      continue;

    return true;
  }

  return false;
}

unsigned LTHLInstrInfo::insertBranch(MachineBasicBlock &MBB,
                                      MachineBasicBlock *TBB,
                                      MachineBasicBlock *FBB,
                                      ArrayRef<MachineOperand> Cond,
                                      const DebugLoc &DL,
                                      int *BytesAdded) const {
  assert(TBB && "insertBranch must not be told to insert a fallthrough");
  assert(Cond.size() <= 1 &&
         "LTHL branch conditions have at most one component");
  assert(!BytesAdded && "code size not handled");

  if (Cond.empty()) {
    // Unconditional branch.
    assert(!FBB && "Unconditional branch with multiple successors!");
    BuildMI(&MBB, DL, get(LTHL::J)).addMBB(TBB);
    return 1;
  }

  // Conditional branch.
  BuildMI(&MBB, DL, get(getCondBranchOpcode(Cond[0].getImm()))).addMBB(TBB);
  unsigned Count = 1;

  if (FBB) {
    // Two-way conditional branch -- insert the fallback unconditional
    // jump to the other side. LTHL has no negated-condition branch (see
    // reverseBranchCondition's comment in LTHLInstrInfo.h), so unlike a
    // richer ISA this can never be collapsed into a single instruction
    // -- it's always genuinely two.
    BuildMI(&MBB, DL, get(LTHL::J)).addMBB(FBB);
    ++Count;
  }
  return Count;
}

unsigned LTHLInstrInfo::removeBranch(MachineBasicBlock &MBB,
                                      int *BytesRemoved) const {
  assert(!BytesRemoved && "code size not handled");

  MachineBasicBlock::iterator I = MBB.end();
  unsigned Count = 0;
  while (I != MBB.begin()) {
    --I;
    if (I->isDebugInstr())
      continue;
    if (I->getOpcode() != LTHL::J && getCondFromBranchOpc(I->getOpcode()) < 0)
      break;
    I->eraseFromParent();
    I = MBB.end();
    ++Count;
  }
  return Count;
}

bool LTHLInstrInfo::expandPostRAPseudo(MachineInstr &MI) const {
  switch (MI.getOpcode()) {
  case LTHL::CALL_PSEUDO: {
    MachineBasicBlock &MBB = *MI.getParent();
    DebugLoc DL = MI.getDebugLoc();

    // The real call sequence -- built here, post-RA/post-scheduling (the
    // one point in the pipeline where nothing can separate these
    // instructions again), not earlier via a custom inserter. See
    // CALL_PSEUDO's comment in LTHLInstrInfo.td for why that matters:
    //   LD    R29, 8    ; R29 := 8 (ADDPC's own address + this CALLJ,
    //                   ;   4 bytes each -- the fixed distance from
    //                   ;   ADDPC to the return point)
    //   ADDPC R26, R29  ; R26 := r31 (ADDPC's own address, per
    //                   ;   arch-base.md's confirmed PC-read semantics:
    //                   ;   an R-type instruction reading r31 gets its
    //                   ;   own address, not yet incremented) + R29
    //   CALLJ $target   ; jump
    // R29 is LTHL's dedicated post-RA scratch register -- see
    // LTHLFrameLowering::adjustReg for the same convention; it's
    // globally reserved, so nothing else can be live in it here.
    MachineInstr &LoadOffset =
        *BuildMI(MBB, MI, DL, get(LTHL::LD), LTHL::R29).addImm(8);
    BuildMI(MBB, MI, DL, get(LTHL::ADDPC), LTHL::R26).addReg(LTHL::R29);

    // CALL_PSEUDO was declared with just (ins GPR:$target), but the
    // LTHLcall SDNode it's selected from is variadic -- LowerCall also
    // hangs argument-register uses and an optional call-preserved
    // regmask off of it, which the Pat rule in LTHLInstrInfo.td already
    // carried over onto this MI as implicit operands beyond operand 0
    // (glue doesn't survive this far -- it's long gone post-isel). Copy
    // them all onto CALLJ, the instruction that actually performs the
    // call, untouched (this also preserves flags like kill on operand 0,
    // unlike rebuilding it with .addReg()).
    MachineInstrBuilder CallJ = BuildMI(MBB, MI, DL, get(LTHL::CALLJ));
    for (const MachineOperand &MO : MI.operands())
      CallJ.add(MO);

    // Bundle the three so no later pass (e.g. a post-RA scheduler, if
    // one's ever enabled) can split them apart and break the hardcoded
    // offset above. NOTE: this used to call a class named
    // `MIBundleBuilder(MBB, LoadOffset, *CallJ.getInstr())` -- that type
    // doesn't exist in this LLVM tree's MachineInstrBundle.h (only the
    // free functions below do; `MIBundleBuilder` was real in older LLVM
    // versions but is gone here), so that call could not have been
    // compiling. finalizeBundle's LastMI argument is exclusive (one past
    // the last instruction in the bundle), unlike the old (nonexistent)
    // API's inclusive range.
    finalizeBundle(MBB, LoadOffset.getIterator(),
                   std::next(CallJ.getInstr()->getIterator()));

    MBB.erase(MI);
    return true;
  }
  case LTHL::RET_PSEUDO: {
    // See RET_PSEUDO's comment in LTHLInstrInfo.td -- by this point PEI
    // has already inserted R26's callee-saved restore (for non-leaf
    // functions) immediately before this pseudo, so building the real
    // RET here, in the same spot, gives its Uses=[R26] a reaching
    // definition that's always local and never has to survive the
    // whole function.
    MachineBasicBlock &MBB = *MI.getParent();
    DebugLoc DL = MI.getDebugLoc();
    MachineInstrBuilder RetMI = BuildMI(MBB, MI, DL, get(LTHL::RET));
    // RET_PSEUDO has no formal operands, but LowerReturn may have
    // attached implicit-use return-value register operands beyond the
    // glue (see LTHLISelLowering::LowerReturn's RetOps) -- carry them
    // over untouched, same "copy every operand across" discipline
    // CALL_PSEUDO's CALLJ expansion uses just above.
    for (const MachineOperand &MO : MI.operands())
      RetMI.add(MO);
    MBB.erase(MI);
    return true;
  }
  case LTHL::ADDRFI: {
    // By this point LTHLRegisterInfo::eliminateFrameIndex (which runs
    // during PEI, well before this post-RA pass) has already replaced
    // ADDRFI's $fi operand in place with a real register -- either the
    // frame register directly (slot offset 0) or R29 holding a
    // materialized SP+offset address (see eliminateFrameIndex and
    // LTHLFrameLowering::adjustReg). All that's left is the same `mov`
    // idiom copyPhysReg uses: `add $dst, $resolved, r0`.
    MachineBasicBlock &MBB = *MI.getParent();
    DebugLoc DL = MI.getDebugLoc();
    Register Dst = MI.getOperand(0).getReg();
    Register Resolved = MI.getOperand(1).getReg();
    BuildMI(MBB, MI, DL, get(LTHL::ADD), Dst)
        .addReg(Resolved)
        .addReg(LTHL::R0);
    MBB.erase(MI);
    return true;
  }
  }
  return false;
}
