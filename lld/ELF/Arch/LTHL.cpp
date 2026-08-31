//===- LTHL.cpp -------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// LTHL (LowtoHighLevel) is a 32-bit custom architecture. The MC layer that
// produces the object files consumed here lives under
// llvm/lib/Target/LTHL/MCTargetDesc/ (AsmParser -> MCCodeEmitter ->
// MCAsmBackend -> ELFObjectWriter). Doc source: arch-base.md v2
// (https://raw.githubusercontent.com/LowtoHighLevel/markdown-source/refs/heads/main/docs/v2/arch-base.md).
//
//===----------------------------------------------------------------------===//

#include "Symbols.h"
#include "Target.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Support/Endian.h"

using namespace llvm;
using namespace llvm::object;
using namespace llvm::support::endian;
using namespace llvm::ELF;
using namespace lld;
using namespace lld::elf;

namespace {
class LTHL final : public TargetInfo {
public:
  LTHL(Ctx &);
  RelExpr getRelExpr(RelType type, const Symbol &s,
                      const uint8_t *loc) const override;
  void relocate(uint8_t *loc, const Relocation &rel,
                uint64_t val) const override;
};
} // namespace

LTHL::LTHL(Ctx &ctx) : TargetInfo(ctx) {
  // Gap, not a fix: arch-base.md defines no illegal-instruction/trap
  // behavior (EXT1/EXT2 are "undefined", not "trap"), so unlike MSP430's
  // borrowed `mov.b #0,r3`, there is no real instruction here that acts as
  // a trap. Falling back to the all-zero NOP encoding (`add r0,r0,r0`,
  // same pattern LTHLAsmBackend::writeNopData uses) -- this pads
  // executable output sections with genuine no-ops, not traps, so
  // accidental execution of padding won't be caught the way it would be
  // on a target with a real trap instruction.
  trapInstr = {0x00, 0x00, 0x00, 0x00};
}

RelExpr LTHL::getRelExpr(RelType type, const Symbol &s,
                          const uint8_t *loc) const {
  switch (type) {
  case R_LTHL_PC16:
    return R_PC;
  default:
    return R_ABS;
  }
}

void LTHL::relocate(uint8_t *loc, const Relocation &rel, uint64_t val) const {
  switch (rel.type) {
  case R_LTHL_NONE:
    break;
  case R_LTHL_PC16: {
    // FormJRL's SHORT-REL: 16-bit signed byte offset relative to the
    // branch instruction's own address -- confirmed against a real LTHL
    // emulator (notes_8 SS5D), not PC+4 like MSP430's word-scaled
    // R_MSP430_10_PCREL. Occupies the low 16 bits of the 32-bit
    // little-endian word (matches fixup_lthl_pc16, TargetOffset=0 in
    // LTHLAsmBackend.cpp).
    checkInt(ctx, loc, (int64_t)val, 16, rel);
    uint32_t insn = read32le(loc);
    write32le(loc, (insn & 0xFFFF0000) | (val & 0xFFFF));
    break;
  }
  case R_LTHL_ABS24: {
    // FormI's simm24 (LD's immediate when it's a symbol expression), low
    // 24 bits of the word.
    checkIntUInt(ctx, loc, val, 24, rel);
    uint32_t insn = read32le(loc);
    write32le(loc, (insn & 0xFF000000) | (val & 0xFFFFFF));
    break;
  }
  case R_LTHL_ABS32:
    // Reserved for `calltarget`, which isn't attached to any concrete
    // instruction yet (see LTHLFixupKinds.h) -- this path is currently
    // unreachable from the emitter, same status as in
    // LTHLAsmBackend::adjustFixupValue. Implemented ahead of that so
    // relaxation/linking won't need a second pass through this file once
    // it is attached.
    checkIntUInt(ctx, loc, val, 32, rel);
    write32le(loc, val);
    break;
  default:
    Err(ctx) << getErrorLoc(ctx, loc) << "unrecognized relocation " << rel.type;
  }
}

void elf::setLTHLTargetInfo(Ctx &ctx) { ctx.target.reset(new LTHL(ctx)); }
