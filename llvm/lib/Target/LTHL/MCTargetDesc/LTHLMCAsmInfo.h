//===--- LTHLMCAsmInfo.h --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_LTHL_MCTARGETDESC_LTHLMCASMINFO_H
#define LLVM_LIB_TARGET_LTHL_MCTARGETDESC_LTHLMCASMINFO_H

#include "llvm/MC/MCAsmInfoELF.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/TargetParser/Triple.h"
namespace llvm {

class LTHLMCAsmInfo : public MCAsmInfoELF {
public:
  explicit LTHLMCAsmInfo(const Triple &TT, const MCTargetOptions &Options);
};

class LTHLMCAsmInfoNoABI : public MCAsmInfo {
public:
  explicit LTHLMCAsmInfoNoABI(const Triple &TT, const MCTargetOptions &Options);
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_LTHL_MCTARGETDESC_LTHLMCASMINFO_H
