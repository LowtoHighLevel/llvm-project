
//===-- LTHLISelDAGToDAG.cpp - A dag to dag inst selector for LTHL ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//
// Implements createLTHLISelDag (declared in LTHL.h), the pass factory
// LTHLPassConfig::addInstSelector() already calls.
//
//===----------------------------------------------------------------------===//

#include "LTHL.h"
#include "LTHLTargetMachine.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/SelectionDAGISel.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "lthl-isel"
#define PASS_NAME "LTHL DAG->DAG Pattern Instruction Selection"

namespace {

class LTHLDAGToDAGISel : public SelectionDAGISel {
public:
  LTHLDAGToDAGISel() = delete;

  explicit LTHLDAGToDAGISel(LTHLTargetMachine &TM, CodeGenOptLevel OptLevel)
      : SelectionDAGISel(TM, OptLevel) {}

private:

#include "LTHLGenDAGISel.inc"

  void Select(SDNode *N) override;

  // Custom selection for a bare ISD::FrameIndex -- see this file's
  // header comment for why this is the one thing table-driven matching
  // can't handle.
  void selectFrameIndex(SDNode *N);

  // Custom selection for an out-of-range ISD::Constant -- returns false
  // (falls through to SelectCode's normal LD Pat) for anything that
  // fits simm24. See the definition for why this can't be a
  // TargetLowering::Custom hook instead.
  bool selectConstant(SDNode *N);
};

class LTHLDAGToDAGISelLegacy : public SelectionDAGISelLegacy {
public:
  static char ID;
  explicit LTHLDAGToDAGISelLegacy(LTHLTargetMachine &TM,
                                   CodeGenOptLevel OptLevel)
      : SelectionDAGISelLegacy(
            ID, std::make_unique<LTHLDAGToDAGISel>(TM, OptLevel)) {}
};

} // namespace

char LTHLDAGToDAGISelLegacy::ID = 0;

INITIALIZE_PASS(LTHLDAGToDAGISelLegacy, DEBUG_TYPE, PASS_NAME, false, false)

FunctionPass *llvm::createLTHLISelDag(LTHLTargetMachine &TM) {
  return new LTHLDAGToDAGISelLegacy(TM, TM.getOptLevel());
}

void LTHLDAGToDAGISel::Select(SDNode *Node) {
  if (Node->isMachineOpcode()) {
    LLVM_DEBUG(errs() << "== "; Node->dump(CurDAG); errs() << "\n");
    Node->setNodeId(-1);
    return;
  }

  switch (Node->getOpcode()) {
  case ISD::FrameIndex:
    selectFrameIndex(Node);
    return;
  case ISD::Constant:
    if (selectConstant(Node))
      return;
    break;
  default:
    break;
  }

  // Everything else is fully table-driven -- see this file's header
  // comment.
  SelectCode(Node);
}

void LTHLDAGToDAGISel::selectFrameIndex(SDNode *Node) {
  SDLoc DL(Node);
  int FI = cast<FrameIndexSDNode>(Node)->getIndex();
  EVT VT = Node->getValueType(0);
  SDValue TFI = CurDAG->getTargetFrameIndex(FI, VT);

  if (Node->hasOneUse()) {
    CurDAG->SelectNodeTo(Node, LTHL::ADDRFI, VT, TFI);
    return;
  }
  ReplaceNode(Node, CurDAG->getMachineNode(LTHL::ADDRFI, DL, VT, TFI));
}

// LD's immediate is a sign-extending 24-bit field (simm24) -- it can
// only reproduce constants whose bits[31:23] are all equal. Constants
// outside that range used to reach LD's table-driven Pat with no range
// check at all and get silently corrupted by the hardware's own
// sign-extension (e.g. materializing 0x00FF0000 actually produced
// 0xFFFF0000 in the register, since bit 23 of 0xFF0000 is set).
//
// In-range constants (the overwhelming common case) are left
// completely alone here -- returns false, falls through to SelectCode's
// normal LD Pat exactly as before. Only the out-of-range case is
// intercepted, building the value one byte at a time instead
// (Horner's-method: LD the top byte, then SHL-by-8/OR the remaining
// three in, reusing SHL_PSEUDO's existing runtime-loop expansion).
// Every individual byte (0-255) trivially fits simm24, so there's no
// range concern building the pieces.
//
// This has to live here rather than as a TargetLowering::Custom hook:
// an earlier attempt marked ISD::Constant Custom and returned a null
// SDValue for the in-range case, intending "leave this node alone" --
// but for ISD::Constant specifically, Custom declining like that falls
// through to the *generic Expand path*, which builds a ConstantPool
// load. LTHL has no addressing mode for that at all, so *every*
// constant started crashing ("Cannot select: ConstantPool<i32 ...>"),
// not just the out-of-range ones the fix was meant for. Intercepting
// directly in Select(), the same way selectFrameIndex does for
// ISD::FrameIndex, sidesteps that legalizer fallback entirely: in-range
// constants never reach any target hook here, so there's nothing for
// the legalizer to second-guess.
bool LTHLDAGToDAGISel::selectConstant(SDNode *Node) {
  auto *C = cast<ConstantSDNode>(Node);
  int32_t Val = static_cast<int32_t>(C->getSExtValue());
  if (isInt<24>(Val))
    return false;

  SDLoc DL(Node);
  EVT VT = Node->getValueType(0);
  uint32_t UVal = static_cast<uint32_t>(Val);

  auto LoadByte = [&](uint32_t Byte) -> SDValue {
    SDValue Imm = CurDAG->getTargetConstant(Byte, DL, MVT::i32);
    return SDValue(CurDAG->getMachineNode(LTHL::LD, DL, VT, Imm), 0);
  };

  // SHL_PSEUDO's shift amount is a GPR operand -- LTHL has no
  // immediate-shift instruction -- so "8" needs its own LD too.
  SDValue Eight = LoadByte(8);

  SDValue Acc = LoadByte((UVal >> 24) & 0xFFu);
  for (int Shift = 16; Shift >= 0; Shift -= 8) {
    SDValue Shifted = SDValue(
        CurDAG->getMachineNode(LTHL::SHL_PSEUDO, DL, VT, Acc, Eight), 0);
    SDValue Byte = LoadByte((UVal >> Shift) & 0xFFu);
    Acc = SDValue(CurDAG->getMachineNode(LTHL::OR, DL, VT, Shifted, Byte), 0);
  }

  ReplaceNode(Node, Acc.getNode());
  return true;
}
