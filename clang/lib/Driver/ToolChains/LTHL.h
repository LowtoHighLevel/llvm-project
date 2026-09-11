//===--- LTHL.h - LTHL ToolChain Implementations ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A minimal, self-contained LTHL ToolChain -- deliberately NOT built on top
// of BareMetal.cpp. BareMetal.cpp is the more standard modern home for a
// no-OS/no-libc target like this one, but its handlesTarget()/linker-flag
// logic (GCC-multilib detection, a per-arch -m<emulation> lookup for GNU ld,
// RISC-V-specific relax flags, etc.) is hard-wired to ARM/AArch64/RISC-V/
// PPC/x86. Extending it for LTHL would mean adding arch-specific branches to
// code several other targets share. A small LTHL-only ToolChain is more
// code we own outright, but touches nothing else.
//
// Reference: MSP430ToolChain (MSP430.h/.cpp) is the closest shape in-tree --
// small target, Generic_ELF base, its own Linker tool. This strips out
// everything MSP430 has that LTHL doesn't yet: no GCCInstallation/multilib
// detection (no msp430-elf-gcc-shaped external toolchain exists for LTHL),
// no crt0/crtbegin/crtend, no libc (LTHLISelLowering.cpp's LowerCall and
// LowerFormalArguments both report_fatal_error on varargs, and there's no
// libc story at all yet). What's left: let Clang's integrated assembler and
// cc1 handle compile+assemble (both already exist and are wired up -- see
// LTHLAsmParser and LTHLMCCodeEmitter), then hand raw object files straight
// to ld.lld.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_LTHL_H
#define LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_LTHL_H

#include "Gnu.h"
#include "clang/Driver/Tool.h"
#include "clang/Driver/ToolChain.h"

namespace clang {
namespace driver {
namespace tools {
namespace lthl {

class LLVM_LIBRARY_VISIBILITY Linker final : public Tool {
public:
  Linker(const ToolChain &TC) : Tool("lthl::Linker", "ld.lld", TC) {}

  bool hasIntegratedCPP() const override { return false; }
  bool isLinkJob() const override { return true; }

  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const llvm::opt::ArgList &TCArgs,
                    const char *LinkingOutput) const override;
};

} // namespace lthl
} // namespace tools

namespace toolchains {

class LLVM_LIBRARY_VISIBILITY LTHLToolChain : public Generic_ELF {
public:
  LTHLToolChain(const Driver &D, const llvm::Triple &Triple,
               const llvm::opt::ArgList &Args);

protected:
  Tool *buildLinker() const override;
};

} // namespace toolchains
} // namespace driver
} // namespace clang

#endif // LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_LTHL_H
