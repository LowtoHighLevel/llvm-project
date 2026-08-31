//===-- LTHLTargetInfo.h - LTHL Target Implementation ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Declares the accessor for the global llvm::Target object representing
// LTHL. Every other file that needs to register something with LLVM's
// TargetRegistry (MC info, AsmPrinter, TargetMachine, ...) includes this.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_LTHL_TARGETINFO_LTHLTARGETINFO_H
#define LLVM_LIB_TARGET_LTHL_TARGETINFO_LTHLTARGETINFO_H

namespace llvm {

class Target;

Target &getTheLTHLTarget();

} // namespace llvm

#endif // LLVM_LIB_TARGET_LTHL_TARGETINFO_LTHLTARGETINFO_H
