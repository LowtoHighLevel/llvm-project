//===-- LTHLFixupKinds.h - LTHL Specific Fixup Entries ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_LTHL_MCTARGETDESC_LTHLFIXUPKINDS_H
#define LLVM_LIB_TARGET_LTHL_MCTARGETDESC_LTHLFIXUPKINDS_H

#include "llvm/MC/MCFixup.h"

#undef LTHL

namespace llvm {
namespace LTHL {

// This table must stay in the same order as MCFixupKindInfo Infos[] in
// LTHLAsmBackend.cpp's getFixupKindInfo(). LTHLAsmBackend.cpp now
// exists and calls applyFixup on these (see its file comment); this
// header only defines the kind IDs themselves, used by both
// LTHLMCCodeEmitter.cpp (to tag the fixups it creates) and
// LTHLAsmBackend.cpp/LTHLELFObjectWriter.cpp (to resolve/relocate them).
enum Fixups {
  // A 16-bit signed, PC-relative fixup for J/JZ/JC/JV/JN's `rel`
  // operand (FormJRL, arch-base.md's SHORT-REL field). Per
  // arch-base.md, SHORT-REL is "relative to current address" -- by the
  // same convention arch-base.md establishes for ADDPC's r31 read (an
  // R-type instruction reading r31 gets *that instruction's own*
  // address, not yet incremented, per LTHLInstrInfo.td's ADDPC
  // comment), this is computed as (target - address of this J*
  // instruction itself), NOT PC+4. ASSUMPTION, not yet independently
  // confirmed against real hardware/an assembler-behavior spec for this
  // specific field -- flagged here for Cory to verify.
  fixup_lthl_pc16 = FirstTargetFixupKind,

  // A 24-bit absolute fixup for LD's `imm` operand (simm24) when it
  // carries a symbol expression (e.g. `ld r5, some_label`) rather than
  // a compile-time constant. This is the gap LTHLAsmParser.cpp's
  // isSImm24()/SImm24AsmOperand comments flag ("there's no relocation
  // support yet to range-check it against") and LTHLInstrInfo.td's
  // simm24 comment calls out (LD's DAG-level Pattern has no width
  // check either) -- this fixup is the missing piece that lets that
  // case round-trip through the emitter instead of silently reaching
  // getMachineOpValue with no way to represent it. No range check is
  // applied at fixup-application time; same as the parser, that's
  // deferred until AsmBackend exists.
  fixup_lthl_imm24,

  // A 32-bit absolute fixup for the `calltarget` operand class defined
  // in LTHLInstrInfo.td. Currently UNUSED -- no concrete instruction
  // in LTHLInstrInfo.td actually has an operand of type `calltarget`
  // today (CALLJ takes a real GPR:$target register operand instead,
  // per LTHLInstrInfo.td's CALL_PSEUDO/CALLJ comments: only
  // register-held function pointers are lowered right now, and
  // LTHLISelLowering's LowerCall report_fatal_errors on GlobalAddress/
  // ExternalSymbol call targets -- see the backend notes' "Known open
  // issues" list). Reserved for whenever that gap is closed and a
  // relocatable call-target operand is wired up to a real instruction.
  fixup_lthl_calltarget,

  // Marker
  LastTargetFixupKind,
  NumTargetFixupKinds = LastTargetFixupKind - FirstTargetFixupKind
};

} // namespace LTHL
} // namespace llvm

#endif // LLVM_LIB_TARGET_LTHL_MCTARGETDESC_LTHLFIXUPKINDS_H
