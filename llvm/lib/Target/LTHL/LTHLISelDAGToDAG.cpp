
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
