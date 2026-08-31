//===-- LTHLSelectionDAGInfo.cpp - LTHL SelectionDAG Info ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LTHLSelectionDAGInfo.h"
#include "LTHLISelLowering.h"

using namespace llvm;

LTHLSelectionDAGInfo::~LTHLSelectionDAGInfo() = default;

bool LTHLSelectionDAGInfo::isTargetMemoryOpcode(unsigned Opcode) const {
  switch (static_cast<LTHLISD::NodeType>(Opcode)) {
  case LTHLISD::LOAD8:
  case LTHLISD::LOAD16:
  case LTHLISD::STORE8:
  case LTHLISD::STORE16:
    return true;
  default:
    return false;
  }
}
