//===--- LTHL.cpp - Implement LTHL target feature support ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the LTHLTargetInfo object.
//
//===----------------------------------------------------------------------===//

#include "LTHL.h"
#include "clang/Basic/MacroBuilder.h"
#include "llvm/ADT/StringSwitch.h"

using namespace clang;
using namespace clang::targets;

const char *const LTHLTargetInfo::GCCRegNames[] = {
    "r0",  "r1",  "r2",  "r3",  "r4",  "r5",  "r6",  "r7",
    "r8",  "r9",  "r10", "r11", "r12", "r13", "r14", "r15",
    "r16", "r17", "r18", "r19", "r20", "r21", "r22", "r23",
    "r24", "r25", "r26", "r27", "r28", "r29", "r30", "r31"};

ArrayRef<const char *> LTHLTargetInfo::getGCCRegNames() const {
  return llvm::ArrayRef(GCCRegNames);
}

bool LTHLTargetInfo::isValidCPUName(StringRef Name) const {
  return llvm::StringSwitch<bool>(Name)
      .Case("generic", true)
      .Case("generic-int", true)
      .Default(false);
}

void LTHLTargetInfo::fillValidCPUList(
    SmallVectorImpl<StringRef> &Values) const {
  Values.emplace_back("generic");
  Values.emplace_back("generic-int");
}

bool LTHLTargetInfo::setCPU(StringRef Name) {
  // Unlike Lanai's CK_NONE-as-failure-sentinel pattern, there's no need for
  // a separate invalid-CPU enumerator here: only two CPU names exist, and
  // an unrecognized one should leave CPU untouched (defaulting to generic)
  // while still reporting failure so TargetInfo::CreateTargetInfo emits
  // err_target_unknown_cpu.
  bool Valid = Name == "generic" || Name == "generic-int";
  if (Valid)
    CPU = Name == "generic-int" ? CK_GENERIC_INT : CK_GENERIC;
  return Valid;
}

bool LTHLTargetInfo::hasFeature(StringRef Feature) const {
  return llvm::StringSwitch<bool>(Feature)
      .Case("lthl", true)
      .Case("int", CPU == CK_GENERIC_INT)
      .Default(false);
}

void LTHLTargetInfo::getTargetDefines(const LangOptions &Opts,
                                      MacroBuilder &Builder) const {
  // Define __lthl__ (and, for symmetry with targets like __AVR__, the
  // all-caps spelling too) when building for target lthl.
  Builder.defineMacro("__lthl__");
  Builder.defineMacro("__LTHL__");

  if (CPU == CK_GENERIC_INT)
    Builder.defineMacro("__LTHL_INT__");
}
