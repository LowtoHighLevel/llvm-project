//===-- LTHLMCTargetDesc.h -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_LTHL_MCTARGETDESC_LTHLMCTARGETDESC_H
#define LLVM_LIB_TARGET_LTHL_MCTARGETDESC_LTHLMCTARGETDESC_H

#include "llvm/Support/DataTypes.h"

namespace llvm {

class MCAsmBackend;
class MCCodeEmitter;
class MCContext;
class MCInstrInfo;
class MCRegisterInfo;
class MCSubtargetInfo;
class MCTargetOptions;
class Target;
class Triple;
class StringRef;

// Factory functions — defined in LTHLMCTargetDesc.cpp, registered with
// TargetRegistry in LLVMInitializeLTHLTargetMC().
MCInstrInfo *createLTHLMCInstrInfo();
MCRegisterInfo *createLTHLMCRegisterInfo(const Triple &TT);
MCSubtargetInfo *createLTHLMCSubtargetInfo(const Triple &TT, StringRef CPU,
                                            StringRef FS);

// Defined in LTHLMCCodeEmitter.cpp (not LTHLMCTargetDesc.cpp, like the
// others above) -- same convention MSP430MCTargetDesc.h/
// MSP430MCCodeEmitter.cpp use, since the emitter needs its own .cpp for
// LTHLGenMCCodeEmitter.inc's #include.
MCCodeEmitter *createLTHLMCCodeEmitter(const MCInstrInfo &MCII,
                                        MCContext &Ctx);

// Defined in LTHLAsmBackend.cpp -- registered via
// TargetRegistry::RegisterMCAsmBackend in LLVMInitializeLTHLTargetMC().
// The ELF-relocation half (createLTHLELFObjectWriter) lives in
// LTHLAsmBackend.h, same file-split MSP430/Lanai use.
MCAsmBackend *createLTHLMCAsmBackend(const Target &T,
                                      const MCSubtargetInfo &STI,
                                      const MCRegisterInfo &MRI,
                                      const MCTargetOptions &Options);

}

// Pull in the TableGen-generated enums (register numbers, instruction
// opcodes, subtarget feature bits). Guard macros match what llvm-tblgen
// emits; anything that includes this header gets LTHL::R0, LTHL::ADD, etc.
#define GET_REGINFO_ENUM
#include "LTHLGenRegisterInfo.inc"

// GET_INSTRINFO_MC_HELPER_DECLS declares LTHL_MC::verifyInstructionPredicates
// (defined under GET_INSTRINFO_MC_DESC/ENABLE_INSTR_PREDICATE_VERIFIER in
// LTHLMCTargetDesc.cpp), which LTHLAsmPrinter::emitInstruction calls --
// same split MSP430MCTargetDesc.h/.cpp use.
#define GET_INSTRINFO_ENUM
#define GET_INSTRINFO_MC_HELPER_DECLS
#include "LTHLGenInstrInfo.inc"

#define GET_SUBTARGETINFO_ENUM
#include "LTHLGenSubtargetInfo.inc"

#endif // LLVM_LIB_TARGET_LTHL_MCTARGETDESC_LTHLMCTARGETDESC_H
