//===-- LTHLMCTargetDesc.cpp --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/LTHLMCTargetDesc.h"
#include "MCTargetDesc/LTHLMCAsmInfo.h"
#include "LTHLInstPrinter.h"
#include "TargetInfo/LTHLTargetInfo.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

// ENABLE_INSTR_PREDICATE_VERIFIER makes this block actually define the
// LTHL_MC::verifyInstructionPredicates body (rather than a no-op stub);
// LTHLAsmPrinter::emitInstruction calls it. LTHLInstrInfo.td doesn't
// gate any instruction on `Predicates = [...]` today (FeatureInt exists
// but nothing checks it via Predicates), so in practice every call
// currently succeeds trivially -- this is still worth having wired up
// correctly now, before that changes.
#define GET_INSTRINFO_MC_DESC
#define ENABLE_INSTR_PREDICATE_VERIFIER
#include "LTHLGenInstrInfo.inc"

#define GET_REGINFO_MC_DESC
#include "LTHLGenRegisterInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "LTHLGenSubtargetInfo.inc"

MCInstrInfo *llvm::createLTHLMCInstrInfo() {
  MCInstrInfo *X = new MCInstrInfo();
  InitLTHLMCInstrInfo(X);
  return X;
}

MCRegisterInfo *llvm::createLTHLMCRegisterInfo(const Triple &TT) {
  MCRegisterInfo *X = new MCRegisterInfo();
  // Second argument is the DWARF register number of the return address —
  // adjust LTHL::LR below if your link register def is named differently.
  InitLTHLMCRegisterInfo(X, LTHL::R26);
  return X;
}

MCSubtargetInfo *llvm::createLTHLMCSubtargetInfo(const Triple &TT,
                                                  StringRef CPU,
                                                  StringRef FS) {
  return createLTHLMCSubtargetInfoImpl(TT, CPU, /*TuneCPU=*/CPU, FS);
}

static MCAsmInfo *createLTHLMCAsmInfo(const MCRegisterInfo &MRI,
                                       const Triple &TT,
                                       const MCTargetOptions &Options) {
  if (TT.getObjectFormat() == Triple::ELF)
    return new LTHLMCAsmInfo(TT, Options);
  return new LTHLMCAsmInfoNoABI(TT, Options);
}

static MCInstPrinter *createLTHLMCInstPrinter(const Triple &T,
                                               unsigned SyntaxVariant,
                                               const MCAsmInfo &MAI,
                                               const MCInstrInfo &MII,
                                               const MCRegisterInfo &MRI) {
  return new LTHLInstPrinter(MAI, MII, MRI);
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeLTHLTargetMC() {
  Target &TheTarget = getTheLTHLTarget();

  TargetRegistry::RegisterMCAsmInfo(TheTarget, createLTHLMCAsmInfo);
  TargetRegistry::RegisterMCInstrInfo(TheTarget, createLTHLMCInstrInfo);
  TargetRegistry::RegisterMCRegInfo(TheTarget, createLTHLMCRegisterInfo);
  TargetRegistry::RegisterMCSubtargetInfo(TheTarget,
                                           createLTHLMCSubtargetInfo);
  TargetRegistry::RegisterMCInstPrinter(TheTarget, createLTHLMCInstPrinter);
  TargetRegistry::RegisterMCCodeEmitter(TheTarget, createLTHLMCCodeEmitter);
  TargetRegistry::RegisterMCAsmBackend(TheTarget, createLTHLMCAsmBackend);

  // Still to add: RegisterMCDisassembler, once a Disassembler exists
  // (RegisterMCAsmParser happens separately -- see
  // AsmParser/LTHLAsmParser.cpp's LLVMInitializeLTHLAsmParser(), called
  // from a different LLVMInitializeLTHL... entry point than this one.)
}
