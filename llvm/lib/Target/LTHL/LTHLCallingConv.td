//===-- LTHLCallingConv.td - Calling convention ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

def CC_LTHL : CallingConv<[
  CCIfType<[i1, i8, i16], CCPromoteToType<i32>>,

  // First four i32 arguments go in R1-R4.
  CCIfType<[i32], CCAssignToReg<[R1, R2, R3, R4]>>,

  // Anything left over is passed on the stack, 4-byte aligned/sized slots.
  CCAssignToStack<4, 4>
]>;

def RetCC_LTHL : CallingConv<[
  CCIfType<[i1, i8, i16], CCPromoteToType<i32>>,

  // Return values go in r1. (r26 is the link/return-*address* register,
  // not a return-value register -- keep those two roles separate.)
  CCIfType<[i32], CCAssignToReg<[R1]>>
]>;

// Callee-saved registers

def CSR : CalleeSavedRegs<(add R8, R9, R10, R11, R12, R13, R14, R26)>;
