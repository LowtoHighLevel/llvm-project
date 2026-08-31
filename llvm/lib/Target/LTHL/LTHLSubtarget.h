//===-- LTHLSubtarget.h - LTHL Subtarget Information -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_LTHL_LTHLSUBTARGET_H
#define LLVM_LIB_TARGET_LTHL_LTHLSUBTARGET_H

#include "LTHLFrameLowering.h"
#include "LTHLISelLowering.h"
#include "LTHLInstrInfo.h"
#include "LTHLRegisterInfo.h"
#include "LTHLSelectionDAGInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/TargetParser/Triple.h"
#include <memory>

#define GET_SUBTARGETINFO_HEADER
#include "LTHLGenSubtargetInfo.inc"

namespace llvm {

class StringRef;
class TargetMachine;

class LTHLSubtarget : public LTHLGenSubtargetInfo {
  // Backing storage for the SubtargetFeature bits TableGen generates from
  // LTHL.td's `def FeatureInt : SubtargetFeature<...>`. ParseSubtargetFeatures
  // (defined in the generated .inc, called from the constructor) sets this
  // based on the -mattr=+int / -mcpu=generic-int string.
  bool HasInt = false;

  LTHLRegisterInfo RegInfo;
  std::unique_ptr<LTHLInstrInfo> InstrInfo;
  std::unique_ptr<LTHLFrameLowering> FrameLoweringInfo;
  std::unique_ptr<LTHLTargetLowering> TLInfo;
  // See LTHLSelectionDAGInfo.h: exists purely so this is never null --
  // SelectionDAG::verifyNode() unconditionally dereferences whatever
  // getSelectionDAGInfo() returns for any target-specific SDNode.
  std::unique_ptr<LTHLSelectionDAGInfo> TSInfo;

public:
  LTHLSubtarget(const Triple &TT, StringRef CPU, StringRef TuneCPU,
                StringRef FS, const TargetMachine &TM);

  // Parses -mattr=/-mcpu= feature strings; auto-generated body, declared
  // by GET_SUBTARGETINFO_HEADER above.
  LTHLSubtarget &initializeSubtargetDependencies(StringRef CPU,
                                                  StringRef TuneCPU,
                                                  StringRef FS);

  // Out-of-line body is generated separately, under GET_SUBTARGETINFO_TARGET_DESC
  // in LTHLSubtarget.cpp (not part of the GET_SUBTARGETINFO_HEADER block
  // included above), so it must be declared here as an ordinary member --
  // TableGen doesn't add it to LTHLGenSubtargetInfo itself.
  void ParseSubtargetFeatures(StringRef CPU, StringRef TuneCPU, StringRef FS);

  bool hasInt() const { return HasInt; }

  const LTHLRegisterInfo *getRegisterInfo() const { return &RegInfo; }

  const LTHLInstrInfo *getInstrInfo() const override { return InstrInfo.get(); }
  const LTHLFrameLowering *getFrameLowering() const override {
    return FrameLoweringInfo.get();
  }
  const LTHLTargetLowering *getTargetLowering() const override {
    return TLInfo.get();
  }
  const LTHLSelectionDAGInfo *getSelectionDAGInfo() const override {
    return TSInfo.get();
  }
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_LTHL_LTHLSUBTARGET_H
