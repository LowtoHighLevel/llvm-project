//===-- LTHLTargetInfo.cpp - LTHL Target Implementation ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Registers the "lthl" target name with LLVM so tools like llc/llvm-mc
// recognize -march=lthl / -mtriple=lthl-*. This is the very first thing
// LLVM's plugin/registry mechanism needs; nothing else works until this
// runs.
//
//===----------------------------------------------------------------------===//

#include "TargetInfo/LTHLTargetInfo.h"
#include "llvm/MC/TargetRegistry.h"

using namespace llvm;

Target &llvm::getTheLTHLTarget() {
  static Target TheLTHLTarget;
  return TheLTHLTarget;
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeLTHLTargetInfo() {
  RegisterTarget<Triple::lthl> X(getTheLTHLTarget(), "lthl",
                                  "LTHL 32-bit fixed-length ISA", "LTHL");
}
