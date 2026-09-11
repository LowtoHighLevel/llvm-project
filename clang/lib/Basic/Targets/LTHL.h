//===--- LTHL.h - Declare LTHL target feature support --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the LTHLTargetInfo object -- the first piece of the
// Clang frontend that knows the `lthl` target exists. Reference targets:
// Lanai.h (closest structural cousin: small, custom 32-bit ISA, no
// pre-existing GCC/binutils target to inherit conventions from) and
// MSP430.h (closest in the LLVM backend itself -- see the LTHL backend's
// own notes for why MSP430 keeps coming up as a reference).
//
// llvm::Triple already fully recognizes `lthl` (ArchType, name parsing,
// default ELF object format, 32-bit pointer width, little-endian, and a
// computeDataLayout() case in TargetDataLayout.cpp) -- this file is what's
// still missing to make `clang -cc1 -triple lthl-unknown-unknown` do
// anything at all.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_BASIC_TARGETS_LTHL_H
#define LLVM_CLANG_LIB_BASIC_TARGETS_LTHL_H

#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/Triple.h"

namespace clang {
namespace targets {

class LLVM_LIBRARY_VISIBILITY LTHLTargetInfo : public TargetInfo {
  // Matches LTHL.td's two ProcessorModels ("generic", "generic-int" --
  // the latter enabling FeatureInt, hardware interrupts). LTHL has no
  // other CPU variants yet.
  enum CPUKind {
    CK_GENERIC,
    CK_GENERIC_INT,
  } CPU = CK_GENERIC;

  static const char *const GCCRegNames[];

public:
  LTHLTargetInfo(const llvm::Triple &Triple, const TargetOptions &)
      : TargetInfo(Triple) {
    // MUST stay byte-for-byte in sync with the `Triple::lthl` case in
    // llvm::Triple::computeDataLayout (llvm/lib/TargetParser/
    // TargetDataLayout.cpp), which is what LTHLTargetMachine actually
    // hands to the backend via TT.computeDataLayout(). If these two ever
    // diverge, -cc1 will emit IR whose data layout doesn't match what the
    // backend expects -- pointer/int size assumptions baked into the
    // frontend (sizeof, struct layout, etc.) would then be silently wrong
    // relative to what codegen produces.
    resetDataLayout("e-m:e-p:32:32-i32:32-i64:32-n32-S32");

    // LTHLAsmParser has no inline-asm dialect variants (no AT&T/Intel-
    // style split -- see LTHLAsmParser.cpp: "no sigils", one syntax only).
    NoAsmVariants = true;

    // No 64-bit integer ALU op exists in hardware, and there's no reason
    // for `long long` to want 8-byte alignment on a target whose bus and
    // registers are all 32-bit -- so it gets the same 32-bit (word)
    // alignment as everything else. TargetInfo's base-class default is
    // LongLongAlign = 64, which does NOT match this target and must be
    // overridden here, or clang aborts at startup
    // ("type long long mapping to i64 has data layout alignment 4 while
    // clang specifies 8") since BackendConsumer::Initialize cross-checks
    // this against the module DataLayout's i64 alignment. MUST stay in
    // sync with the explicit `-i64:32` above and with
    // llvm::Triple::computeDataLayout's Triple::lthl case.
    LongLongAlign = 32;
  }

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;

  bool isValidCPUName(StringRef Name) const override;

  void fillValidCPUList(SmallVectorImpl<StringRef> &Values) const override;

  bool setCPU(StringRef Name) override;

  bool hasFeature(StringRef Feature) const override;

  ArrayRef<const char *> getGCCRegNames() const override;

  ArrayRef<TargetInfo::GCCRegAlias> getGCCRegAliases() const override {
    // LTHLAsmParser recognizes exactly "r0".."r31" today -- no sp/pc/lr
    // aliases exist at the asm level (see LTHL.td: "registers have
    // exactly one spelling each ... no alternate names like sp/pc/lr
    // yet"). Revisit this once/if the AsmParser grows
    // ShouldEmitMatchRegisterAltName support; until then, aliasing "sp"
    // to r30 here would let inline-asm code reference a name the real
    // AsmParser can't actually assemble.
    return {};
  }

  BuiltinVaListKind getBuiltinVaListKind() const override {
    return TargetInfo::VoidPtrBuiltinVaList;
  }

  llvm::SmallVector<Builtin::InfosShard> getTargetBuiltins() const override {
    return {};
  }

  bool validateAsmConstraint(const char *&Name,
                             TargetInfo::ConstraintInfo &Info) const override {
    // No target-specific constraint letters yet -- "r" (any GPR) is all
    // that's needed and TargetInfo's generic handling already covers it.
    // LTHL has no memory-operand kind at all (see
    // LTHLAsmPrinter.cpp's PrintAsmMemoryOperand comment: READ/WRITE take
    // a plain address register, not base+offset), so there is no "m" to
    // support either.
    return false;
  }

  std::string_view getClobbers() const override { return ""; }

  bool hasBitIntType() const override { return true; }
};

} // namespace targets
} // namespace clang

#endif // LLVM_CLANG_LIB_BASIC_TARGETS_LTHL_H
