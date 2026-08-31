//===-- LTHLTargetMachine.cpp - Define TargetMachine for LTHL -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LTHLTargetMachine.h"
#include "LTHL.h"
#include "TargetInfo/LTHLTargetInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/PassRegistry.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Target/TargetOptions.h"
#include <optional>

using namespace llvm;

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeLTHLTarget() {
  RegisterTargetMachine<LTHLTargetMachine> X(getTheLTHLTarget());
  PassRegistry &PR = *PassRegistry::getPassRegistry();
  initializeLTHLDAGToDAGISelLegacyPass(PR);
  initializeLTHLAsmPrinterPass(PR);
}

static Reloc::Model getEffectiveRelocModel(std::optional<Reloc::Model> RM) {
  // LTHL doesn't support PIC codegen (no GOT/PLT machinery, and LD's
  // immediate is a plain constant -- see LTHLISelLowering.cpp's LowerCall
  // comment on why global addresses aren't lowered yet), so Static is the
  // only sensible default.
  return RM.value_or(Reloc::Static);
}

LTHLTargetMachine::LTHLTargetMachine(const Target &T, const Triple &TT,
                                      StringRef CPU, StringRef FS,
                                      const TargetOptions &Options,
                                      std::optional<Reloc::Model> RM,
                                      std::optional<CodeModel::Model> CM,
                                      CodeGenOptLevel OL, bool JIT)
    : CodeGenTargetMachineImpl(T, TT.computeDataLayout(), TT, CPU, FS,
                                Options, getEffectiveRelocModel(RM),
                                getEffectiveCodeModel(CM, CodeModel::Small),
                                OL),
      // Generic ELF object-file lowering. LTHL has no unusual section
      // requirements yet, so there's no LTHLTargetObjectFile subclass --
      // add one if/when that changes.
      TLOF(std::make_unique<TargetLoweringObjectFileELF>()),
      Subtarget(TT, CPU, /*TuneCPU=*/CPU, FS, *this) {
  initAsmInfo();
}

LTHLTargetMachine::~LTHLTargetMachine() = default;

namespace {

class LTHLPassConfig : public TargetPassConfig {
public:
  LTHLPassConfig(LTHLTargetMachine &TM, PassManagerBase &PM)
      : TargetPassConfig(TM, PM) {}

  LTHLTargetMachine &getLTHLTargetMachine() const {
    return getTM<LTHLTargetMachine>();
  }

  bool addInstSelector() override;
};

} // namespace

TargetPassConfig *LTHLTargetMachine::createPassConfig(PassManagerBase &PM) {
  return new LTHLPassConfig(*this, PM);
}

bool LTHLPassConfig::addInstSelector() {
  addPass(createLTHLISelDag(getLTHLTargetMachine()));
  return false;
}
