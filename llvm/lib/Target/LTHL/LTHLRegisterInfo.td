//===-- LTHLRegisterInfo.td - Register information -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
class LTHLReg<bits<16> enc, string n> : Register<n> {
  let HWEncoding{15-0} = enc;
  let Namespace = "LTHL";
}

foreach I = 0-31 in {
  def R#I : LTHLReg<I, "r"#I>;
}

// R30 = stack pointer, R31 = program counter, R26 = link/return-address
// register. R31 is deliberately left OUT of GPR below: the PC isn't a
// normal operand LLVM's register allocator should ever be free to hand
// out, so it's excluded from the class entirely rather than just marked
// reserved. R30 stays IN the class (it needs to appear as a normal
// operand in address calculations, spills, etc.) and gets marked
// reserved instead, in LTHLRegisterInfo::getReservedRegs().
def GPR : RegisterClass<"LTHL", [i32], 32,
    (add R0, R1, R2, R3, R4, R5, R6, R7,
         R8, R9, R10, R11, R12, R13, R14,
	 R15, R16, R17, R18, R19, R20, R21,
	 R22, R23, R24, R25, R26, R27, R28,
	 R29, R30)>;

def ZERO : RegisterClass<"LTHL", [i32], 32, (add R0)>;

// Not a real addressable register -- purely a synthetic Def/Use edge so
// the scheduler and register allocator never separate a flag-setting
// instruction (ADD, SUB -- see their `Defs` in LTHLInstrInfo.td) from
// the jr*/j* that reads the result. Hardware tracks Z/C/V/N together as
// implicit state; LLVM only ever sees this as a single all-or-nothing
// dependency edge, never as a real operand -- deliberately not added to
// any allocatable RegisterClass, so the allocator can't hand it out.
def FLAGS : Register<"flags"> {
  let Namespace = "LTHL";
}
