//===-- LTHL.h - Top level interface ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_LTHL_LTHL_H
#define LLVM_LIB_TARGET_LTHL_LTHL_H

#include "MCTargetDesc/LTHLMCTargetDesc.h"

namespace llvm {

class LTHLTargetMachine;
class FunctionPass;
class PassRegistry;

  
FunctionPass *createLTHLISelDag(LTHLTargetMachine &TM);

void initializeLTHLDAGToDAGISelLegacyPass(PassRegistry &);

void initializeLTHLAsmPrinterPass(PassRegistry &);

} // namespace llvm

#endif // LLVM_LIB_TARGET_LTHL_LTHL_H
