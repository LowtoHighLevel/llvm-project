//===-- LTHLMCASMInfo.cpp ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LTHLMCAsmInfo.h"

using namespace llvm;

LTHLMCAsmInfo::LTHLMCAsmInfo(const Triple &TT, const MCTargetOptions &Options)
    : MCAsmInfoELF(Options) {
  CodePointerSize = 4;
  CalleeSaveStackSlotSize = 4;

  MinInstAlignment = 4;

  CommentString = "#";
  SupportsDebugInformation = true;

  ExceptionsType = ExceptionHandling::None;
}

LTHLMCAsmInfoNoABI::LTHLMCAsmInfoNoABI(const Triple &TT,
				       const MCTargetOptions &Options)
      : MCAsmInfo(Options) {
  CodePointerSize = 4;
  CalleeSaveStackSlotSize = 4;
  MinInstAlignment = 4;
  CommentString = "#";

  SupportsDebugInformation = false;
  ExceptionsType = ExceptionHandling::None;
}
