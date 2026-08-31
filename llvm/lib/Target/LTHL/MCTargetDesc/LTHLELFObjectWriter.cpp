//===-- LTHLELFObjectWriter.cpp - LTHL ELF Writer ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/LTHLAsmBackend.h"
#include "MCTargetDesc/LTHLFixupKinds.h"
#include "MCTargetDesc/LTHLMCTargetDesc.h"

#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

namespace {

class LTHLELFObjectWriter : public MCELFObjectTargetWriter {
public:
  explicit LTHLELFObjectWriter(uint8_t OSABI)
      : MCELFObjectTargetWriter(/*Is64Bit_=*/false, OSABI, ELF::EM_LTHL,
                                 /*HasRelocationAddend_=*/true) {}

  ~LTHLELFObjectWriter() override = default;

protected:
  unsigned getRelocType(const MCFixup &Fixup, const MCValue &Target,
                        bool IsPCRel) const override {
    switch (unsigned(Fixup.getKind())) {
    case LTHL::fixup_lthl_pc16:
      return ELF::R_LTHL_PC16;
    case LTHL::fixup_lthl_imm24:
      return ELF::R_LTHL_ABS24;
    case LTHL::fixup_lthl_calltarget:
      return ELF::R_LTHL_ABS32;
    case FK_Data_1:
    case FK_Data_2:
    case FK_Data_4:
    case FK_Data_8:
      // Generic .byte/.short/.word/.quad directives -- no LTHL-specific
      // relocation kind to distinguish them by, same convention every
      // other target's writer follows for the FK_Data_* fallthrough.
      return ELF::R_LTHL_NONE;
    default:
      llvm_unreachable("Invalid fixup kind for LTHL!");
    }
  }
};

} // end anonymous namespace

std::unique_ptr<MCObjectTargetWriter>
llvm::createLTHLELFObjectWriter(uint8_t OSABI) {
  return std::make_unique<LTHLELFObjectWriter>(OSABI);
}
