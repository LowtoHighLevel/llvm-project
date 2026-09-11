//===--- LTHL.cpp - LTHL ToolChain Implementations -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LTHL.h"
#include "clang/Driver/CommonArgs.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/InputInfo.h"
#include "clang/Options/Options.h"
#include "llvm/Option/ArgList.h"

using namespace clang::driver;
using namespace clang::driver::toolchains;
using namespace clang::driver::tools;
using namespace clang;
using namespace llvm::opt;

/// See LTHL.h for the rationale behind this ToolChain's shape (deliberately
/// minimal, not built on BareMetal.cpp, no GCCInstallation/crt/libc).
LTHLToolChain::LTHLToolChain(const Driver &D, const llvm::Triple &Triple,
                             const ArgList &Args)
    : Generic_ELF(D, Triple, Args) {}

Tool *LTHLToolChain::buildLinker() const {
  return new tools::lthl::Linker(*this);
}

void lthl::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                                const InputInfo &Output,
                                const InputInfoList &Inputs,
                                const ArgList &Args,
                                const char *LinkingOutput) const {
  const ToolChain &ToolChain = getToolChain();
  ArgStringList CmdArgs;

  // No crt0/crtbegin/crtend exist for this target (see LTHL.h) -- so unlike
  // msp430::Linker::AddStartFiles/AddEndFiles there is nothing to add here
  // even when start/end files would normally be requested. -nostartfiles is
  // effectively always in force.

  Args.AddAllArgs(CmdArgs, options::OPT_L);
  ToolChain.AddFilePathLibArgs(Args, CmdArgs);
  AddLinkerInputs(ToolChain, Inputs, Args, CmdArgs, JA);

  // Similarly, no default libraries exist to add (no libc yet -- see
  // LTHL.h). -nostdlib/-nodefaultlibs are accepted like any other target but
  // there is nothing for them to suppress right now.

  Args.AddAllArgs(CmdArgs, options::OPT_T);

  CmdArgs.push_back("-o");
  CmdArgs.push_back(Output.getFilename());

  const char *Exec =
      Args.MakeArgString(ToolChain.GetProgramPath(getShortName()));
  C.addCommand(std::make_unique<Command>(
      JA, *this, ResponseFileSupport::AtFileCurCP(), Exec, CmdArgs, Inputs,
      Output));
}
