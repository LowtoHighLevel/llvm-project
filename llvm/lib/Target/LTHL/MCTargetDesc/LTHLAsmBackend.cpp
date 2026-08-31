//===-- LTHLAsmBackend.cpp - LTHL Assembler Backend ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Closes the gap LTHLMCCodeEmitter.cpp's file comment flags: that
// emitter has been recording fixup_lthl_pc16/imm24/calltarget fixups
// (see LTHLFixupKinds.h) since it was added, but nothing called
// MCFixup::applyFixup on them because no MCAsmBackend existed. This is
// that AsmBackend.
//
//===----------------------------------------------------------------------===//

#include "LTHLAsmBackend.h"
#include "MCTargetDesc/LTHLFixupKinds.h"
#include "MCTargetDesc/LTHLMCTargetDesc.h"

#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {

// Range-check + mask a fixup's resolved Value into the field width the
// encoding actually has room for, per-kind. Doesn't touch anything for
// FK_Data_* (generic .word/.short/.byte directives) -- those pass
// through untouched, same as every other target's adjustFixupValue.
static uint64_t adjustFixupValue(const MCFixup &Fixup, uint64_t Value,
                                  MCContext &Ctx) {
  switch (unsigned(Fixup.getKind())) {
  case LTHL::fixup_lthl_pc16: {
    int64_t SVal = int64_t(Value);
    if (SVal < -32768 || SVal > 32767)
      Ctx.reportError(Fixup.getLoc(),
                       "branch target out of range for 16-bit signed "
                       "PC-relative fixup");
    return Value & 0xFFFF;
  }
  case LTHL::fixup_lthl_imm24: {
    int64_t SVal = int64_t(Value);
    if (SVal < -8388608 || SVal > 8388607)
      Ctx.reportError(Fixup.getLoc(),
                       "value out of range for 24-bit signed immediate "
                       "fixup");
    return Value & 0xFFFFFF;
  }
  case LTHL::fixup_lthl_calltarget:
    // Reserved, unattached to any concrete instruction yet -- see
    // LTHLFixupKinds.h. 32-bit absolute, no narrower than the value
    // itself, so nothing to mask.
    return Value;
  default:
    // FK_Data_1/2/4/8 and friends -- generic directive fixups, no
    // target-specific adjustment.
    return Value;
  }
}

class LTHLAsmBackend : public MCAsmBackend {
  uint8_t OSABI;

public:
  LTHLAsmBackend(const MCSubtargetInfo &STI, uint8_t OSABI)
      : MCAsmBackend(llvm::endianness::little), OSABI(OSABI) {
    // Matches LTHLMCCodeEmitter.cpp's little-endian instruction-word
    // assumption (flagged there as unconfirmed against real
    // hardware/a reference implementation) -- an AsmBackend's Endian
    // must agree with the emitter's byte order or fixups would land
    // in the wrong bytes.
  }
  ~LTHLAsmBackend() override = default;

  void applyFixup(const MCFragment &F, const MCFixup &Fixup,
                  const MCValue &Target, uint8_t *Data, uint64_t Value,
                  bool IsResolved) override;

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override {
    return createLTHLELFObjectWriter(OSABI);
  }

  MCFixupKindInfo getFixupKindInfo(MCFixupKind Kind) const override {
    // clang-format off
    const static MCFixupKindInfo Infos[LTHL::NumTargetFixupKinds] = {
      // This table must stay in the same order as the enum in
      // LTHLFixupKinds.h.
      //
      // name                     offset bits  flags
      // Flags is 0 for all three -- this tree's MCFixupKindInfo has no
      // FKF_IsPCRel-style constant to set (checked; MSP430AsmBackend.cpp's
      // own PC-relative fixup_10_pcrel entry is also flags=0 in this
      // tree), and PC-relative-ness is already fully carried by each
      // MCFixup's own PCRel bit (see LTHLMCCodeEmitter.cpp's
      // getBranchTargetOpValue: `MCFixup::create(..., /*PCRel=*/true)`),
      // which is what applyFixup/getRelocType actually key off of.
      {"fixup_lthl_pc16",         0,     16,    0},
      {"fixup_lthl_imm24",        0,     24,    0},
      {"fixup_lthl_calltarget",   0,     32,    0},
    };
    // clang-format on
    static_assert(std::size(Infos) == LTHL::NumTargetFixupKinds,
                  "Not all fixup kinds added to Infos array");

    if (Kind < FirstTargetFixupKind)
      return MCAsmBackend::getFixupKindInfo(Kind);

    assert(unsigned(Kind - FirstTargetFixupKind) <
               LTHL::NumTargetFixupKinds &&
           "Invalid kind!");
    return Infos[Kind - FirstTargetFixupKind];
  }

  // Every real instruction is exactly 4 bytes (LTHLInst's `let Size =
  // 4;`, unconditional -- see LTHLMCCodeEmitter.h's file comment) and
  // there's no shorter/longer encoding to relax into, so this target
  // never needs relaxation. mayNeedRelaxation's base-class default
  // (always false) is already correct and is left un-overridden here,
  // same as MSP430AsmBackend -- there's no fixupNeedsRelaxationAdvanced
  // override either, for the same reason.

  unsigned getMinimumNopSize() const override {
    // No sub-word nop exists -- every instruction, real or otherwise,
    // is 4 bytes (MinInstAlignment=4, see LTHLMCAsmInfo.cpp). Reporting
    // 1 (the base-class default) would let the assembler ask
    // writeNopData for a 1-3 byte pad, which this target can't
    // produce -- see writeNopData below.
    return 4;
  }

  bool writeNopData(raw_ostream &OS, uint64_t Count,
                    const MCSubtargetInfo *STI) const override;
};

void LTHLAsmBackend::applyFixup(const MCFragment &F, const MCFixup &Fixup,
                                const MCValue &Target, uint8_t *Data,
                                uint64_t Value, bool IsResolved) {
  maybeAddReloc(F, Fixup, Target, Value, IsResolved);
  Value = adjustFixupValue(Fixup, Value, getContext());
  if (!Value)
    return; // Doesn't change the encoding (e.g. an already-zeroed
            // field, or a same-address branch-to-self edge case).

  MCFixupKindInfo Info = getFixupKindInfo(Fixup.getKind());
  // Shift the value into position. Every current LTHL fixup has
  // TargetOffset == 0 (SHORT-REL/imm both occupy bits[N-1:0] of the
  // little-endian 4-byte instruction word -- see LTHLInstrInfo.td's
  // FormJRL/FormI: `Inst{15-0} = rel;` / `Inst{23-0} = imm;`), but this
  // still shifts generically in case that ever changes.
  Value <<= Info.TargetOffset;

  unsigned NumBytes = (Info.TargetSize + Info.TargetOffset + 7) / 8;
  assert(Fixup.getOffset() + NumBytes <= F.getSize() &&
         "Invalid fixup offset!");

  // Mask the fixup's bits into the already-emitted (little-endian)
  // instruction bytes -- getMachineOpValue/getBranchTargetOpValue in
  // LTHLMCCodeEmitter.cpp already emitted 0 for any operand carrying a
  // fixup, so this is a pure OR, not a read-modify-preserve-other-bits
  // dance.
  for (unsigned I = 0; I != NumBytes; ++I)
    Data[I] |= uint8_t((Value >> (I * 8)) & 0xff);
}

bool LTHLAsmBackend::writeNopData(raw_ostream &OS, uint64_t Count,
                                  const MCSubtargetInfo *STI) const {
  if ((Count % 4) != 0)
    return false;

  // `add r0, r0, r0` -- FormR with NEGB=0, CARRYIN=0, aluop=Adder
  // (funct=0b00000), and every register field (outreg/areg/breg) set
  // to r0 -- encodes to all-zero bits (opcode 0x0, funct 0, all three
  // 5-bit register fields 0, every reserved bit already 0 per
  // LTHLInstrInfo.td's FormR). r0 always reads/writes as zero (see
  // arch-base.md's register table), so this is a real, inert
  // instruction on real hardware, not just a convenient bit pattern --
  // the same "mov"-via-ADD idiom the backend already uses elsewhere
  // (see backend notes' `mov $rd, $rs` == `add $rd, $rs, r0` alias)
  // extended to its most degenerate case.
  for (uint64_t I = 0; I < Count; I += 4)
    OS.write("\x00\x00\x00\x00", 4);

  return true;
}

} // end anonymous namespace

MCAsmBackend *llvm::createLTHLMCAsmBackend(const Target &T,
                                            const MCSubtargetInfo &STI,
                                            const MCRegisterInfo &MRI,
                                            const MCTargetOptions &Options) {
  // LTHL has no OS notion at all -- arch-base.md's startup section has
  // the raw binary loaded straight at 0x0 with no loader/OS in the
  // picture, the same situation MSP430AsmBackend.cpp is in (it hardcodes
  // ELF::ELFOSABI_STANDALONE rather than deriving OSABI from the
  // triple's OS field for exactly this reason). Following that same
  // convention here rather than MCELFObjectTargetWriter::getOSABI(),
  // which is meant for targets that actually run under a real OS ABI.
  return new LTHLAsmBackend(STI, ELF::ELFOSABI_STANDALONE);
}
