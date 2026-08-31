//===-- LTHLTargetMachine.h - Define TargetMachine for LTHL -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_LTHL_LTHLTARGETMACHINE_H
#define LLVM_LIB_TARGET_LTHL_LTHLTARGETMACHINE_H

#include "LTHLSubtarget.h"
#include "llvm/CodeGen/CodeGenTargetMachineImpl.h"
#include <memory>
#include <optional>

namespace llvm {

class LTHLTargetMachine : public CodeGenTargetMachineImpl {
  std::unique_ptr<TargetLoweringObjectFile> TLOF;
  LTHLSubtarget Subtarget;

public:
  LTHLTargetMachine(const Target &T, const Triple &TT, StringRef CPU,
                     StringRef FS, const TargetOptions &Options,
                     std::optional<Reloc::Model> RM,
                     std::optional<CodeModel::Model> CM, CodeGenOptLevel OL,
                     bool JIT);
  ~LTHLTargetMachine() override;

  // NOTE: LTHL doesn't vary the subtarget per function yet (e.g. via
  // target-cpu/target-features function attributes) -- every function
  // gets the single Subtarget built from the command-line -mcpu=/-mattr=
  // strings. Revisit if that's ever needed.
  const LTHLSubtarget *getSubtargetImpl(const Function &) const override {
    return &Subtarget;
  }

  TargetPassConfig *createPassConfig(PassManagerBase &PM) override;

  TargetLoweringObjectFile *getObjFileLowering() const override {
    return TLOF.get();
  }
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_LTHL_LTHLTARGETMACHINE_H
