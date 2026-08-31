//===-- LTHLAsmBackend.h - LTHL Assembler Backend --------------*- C++ -*-===//
//
// Declares createLTHLMCAsmBackend (the factory LTHLMCTargetDesc.cpp's
// LLVMInitializeLTHLTargetMC() registers via
// TargetRegistry::RegisterMCAsmBackend) and createLTHLELFObjectWriter
// (the ELF relocation-table half, defined in LTHLELFObjectWriter.cpp --
// same file split MSP430/Lanai use, so LTHLAsmBackend.cpp doesn't need
// to know about ELF::R_* details itself).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_LTHL_MCTARGETDESC_LTHLASMBACKEND_H
#define LLVM_LIB_TARGET_LTHL_MCTARGETDESC_LTHLASMBACKEND_H

#include "llvm/Support/DataTypes.h"
#include <memory>

namespace llvm {

class MCObjectTargetWriter;

/// Defined in LTHLELFObjectWriter.cpp. See that file's header comment
/// for the EM_LTHL/R_LTHL_* placeholder-value caveat -- these numbers
/// are NOT yet registered in llvm/include/llvm/BinaryFormat/ELF.h.
std::unique_ptr<MCObjectTargetWriter> createLTHLELFObjectWriter(uint8_t OSABI);

} // namespace llvm

#endif // LLVM_LIB_TARGET_LTHL_MCTARGETDESC_LTHLASMBACKEND_H
