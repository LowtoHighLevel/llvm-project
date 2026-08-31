//===-- LTHLISelLowering.h - LTHL DAG Lowering Interface -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Declares LTHLTargetLowering, the TargetLowering subclass that:
//   - tells SelectionDAGLegalize which operations LTHL's instruction set
//     actually covers (see the constructor in the .cpp -- most non-basic
//     integer ops have no matching instruction and are marked Expand so
//     they fall back to compiler-rt libcalls; condition-code-based ops
//     like SETCC/SELECT_CC/BR_CC are left unimplemented for now, same as
//     the analyzeBranch stub in LTHLInstrInfo.h notes)
//   - lowers formal arguments / calls / returns using CC_LTHL/RetCC_LTHL
//     from LTHLCallingConv.td
//   - implements EmitInstrWithCustomInserter for the pseudo
//     instructions in LTHLInstrInfo.td marked usesCustomInserter=1
//     (ADDI, BR_CC_PSEUDO -- CALL_PSEUDO is expanded separately and much
//     later, by LTHLInstrInfo::expandPostRAPseudo, since its expansion
//     needs to survive scheduling untouched -- see its comment in
//     LTHLInstrInfo.td)
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_LTHL_LTHLISELLOWERING_H
#define LLVM_LIB_TARGET_LTHL_LTHLISELLOWERING_H

#include "llvm/CodeGen/TargetLowering.h"

namespace llvm {

class LTHLSubtarget;

namespace LTHLISD {
enum NodeType : unsigned {
  FIRST_NUMBER = ISD::BUILTIN_OP_END,

  // Wraps a function call. Chain in, optional glue in, callee (currently
  // always a GPR holding the callee's address -- see the LowerCall
  // comment in the .cpp about direct/global calls not being supported
  // yet), argument-register uses, optional call-preserved regmask, glue
  // out. Selected 1:1 onto CALL_PSEUDO by the Pat in LTHLInstrInfo.td, so
  // LTHLISelDAGToDAG doesn't need any custom C++ for it once that file
  // exists -- table-driven pattern matching handles it.
  CALL,

  // Marks the end of a function: chain in, glued CopyToReg uses for the
  // return-value registers RetCC_LTHL assigned. Selected 1:1 onto RET
  // (jr r26) by the Pat in LTHLInstrInfo.td.
  RET_GLUE,

  // Compare-and-branch: chain in, lhs, rhs, CondCode (as a
  // TargetConstant), destination basic block. Built by LowerBR_CC from
  // ISD::BR_CC. Selected 1:1 onto BR_CC_PSEUDO by the Pat in
  // LTHLInstrInfo.td -- see that def's comment for why this needs a
  // custom-inserted pseudo rather than a plain instruction pattern.
  BR_CC,

  // Sub-word memory access -- see the lthl_load8/lthl_load16/
  // lthl_store8/lthl_store16 SDNode defs and their Pat rules in
  // LTHLInstrInfo.td. Built by LowerLOAD/LowerSTORE (Custom for i8/i16
  // extending-load/truncating-store actions), selected 1:1 onto
  // READ8/READ16/WRITE8/WRITE16. Replaces the old WORD_LOAD/WORD_STORE
  // word-per-variable model, which selected onto plain 32-bit READ/
  // WRITE and silently clobbered neighboring bytes on a sub-word store.
  LOAD8,
  LOAD16,
  STORE8,
  STORE16,

  // Wraps a TargetGlobalAddress/TargetExternalSymbol so it can be fed
  // as the immediate operand of LD (see the Pat rules in
  // LTHLInstrInfo.td). LTHL has no PC-relative or GOT-style addressing
  // -- LD's simm24 field, together with fixup_lthl_imm24 (see
  // LTHLFixupKinds.h), is the only way to get a symbol's address into a
  // register, so this just marks "materialize this symbol via LD"
  // rather than adding any real addressing-mode logic. Built by
  // LowerGlobalAddress/LowerExternalSymbol (ISD::GlobalAddress/
  // ISD::ExternalSymbol are marked Custom in the constructor for
  // exactly this reason) and also used directly by LowerCall for
  // direct calls to a global/external symbol -- see that function's
  // comment.
  WRAPPER,

  // Standalone boolean materialization: lhs, rhs, CondCode (as a
  // TargetConstant) in, i32 0/1 out. Built by LowerSETCC from
  // ISD::SETCC (marked Custom in the constructor). Selected 1:1 onto
  // SETCC_PSEUDO by the Pat in LTHLInstrInfo.td -- see that def's
  // comment, and emitSETCC, for why this needs a custom-inserted
  // pseudo (a branch-based diamond CFG) rather than a plain pattern,
  // same reasoning as BR_CC/BR_CC_PSEUDO.
  SETCC,

  // Conditional-value select: lhs, rhs, CondCode (as a TargetConstant),
  // true-value, false-value in, one of the two values out. Built by
  // LowerSELECT_CC from ISD::SELECT_CC (marked Custom in the
  // constructor); plain ISD::SELECT is marked Expand there, so the
  // legalizer rewrites it into SELECT_CC before this is ever reached.
  // Selected 1:1 onto SELECT_CC_PSEUDO by the Pat in LTHLInstrInfo.td --
  // see that def's comment, and emitSELECT_CC, for why this needs the
  // same branch-based diamond CFG SETCC/SETCC_PSEUDO use.
  SELECT_CC,
};
} // namespace LTHLISD

class LTHLTargetLowering : public TargetLowering {
public:
  explicit LTHLTargetLowering(const TargetMachine &TM,
                               const LTHLSubtarget &STI);

  const char *getTargetNodeName(unsigned Opcode) const override;

  SDValue LowerOperation(SDValue Op, SelectionDAG &DAG) const override;
  
  MachineBasicBlock *
  EmitInstrWithCustomInserter(MachineInstr &MI,
                               MachineBasicBlock *BB) const override;

private:
  SDValue LowerFormalArguments(SDValue Chain, CallingConv::ID CallConv,
                                bool IsVarArg,
                                const SmallVectorImpl<ISD::InputArg> &Ins,
                                const SDLoc &DL, SelectionDAG &DAG,
                                SmallVectorImpl<SDValue> &InVals) const override;

  SDValue LowerCall(TargetLowering::CallLoweringInfo &CLI,
                     SmallVectorImpl<SDValue> &InVals) const override;

  bool CanLowerReturn(CallingConv::ID CallConv, MachineFunction &MF,
                       bool IsVarArg,
                       const SmallVectorImpl<ISD::OutputArg> &Outs,
                       LLVMContext &Context, const Type *RetTy) const override;

  SDValue LowerReturn(SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
                       const SmallVectorImpl<ISD::OutputArg> &Outs,
                       const SmallVectorImpl<SDValue> &OutVals,
                       const SDLoc &DL, SelectionDAG &DAG) const override;

  MachineBasicBlock *emitADDI(MachineInstr &MI, MachineBasicBlock *BB) const;
  MachineBasicBlock *emitBR_CC(MachineInstr &MI, MachineBasicBlock *BB) const;
  MachineBasicBlock *emitSETCC(MachineInstr &MI, MachineBasicBlock *BB) const;
  MachineBasicBlock *emitSELECT_CC(MachineInstr &MI,
                                    MachineBasicBlock *BB) const;

  // Shared by emitBR_CC/emitSETCC: builds the jr*/j* sequence for a
  // given CondCode, inserted into BB immediately before MI, branching
  // to Dest when the (already-computed-by-SUB) flags satisfy CC and
  // falling through to FalseSucc otherwise. Signed comparisons
  // (SETLT/LE/GT/GE) may allocate and insert one extra basic block
  // (immediately after BB) to evaluate N!=V -- see the definition for
  // why. Does not touch MI itself; callers erase it once all their own
  // pseudo-specific setup (SUB, result materialization, etc.) is done.
  void emitCondBranch(MachineBasicBlock *BB, MachineInstr &MI,
                       ISD::CondCode CC, MachineBasicBlock *Dest,
                       MachineBasicBlock *FalseSucc) const;

  // Shared by SHL_PSEUDO/SRL_PSEUDO/SRA_PSEUDO -- see the comment above
  // those defs in LTHLInstrInfo.td and emitShift's definition in the
  // .cpp for the generated loop's shape.
  enum class ShiftKind { SHL, SRL, SRA };
  MachineBasicBlock *emitShift(MachineInstr &MI, MachineBasicBlock *BB,
                                ShiftKind Kind) const;

  SDValue LowerBR_CC(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSETCC(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSELECT_CC(SDValue Op, SelectionDAG &DAG) const;

  // LTHL's WRITE8/READ8/WRITE16/READ16 give native sub-word memory
  // access (see MemWidth in LTHLInstrInfo.td) -- there is no more
  // word-per-variable workaround. These implement TruncStoreAction/
  // LoadExtAction == Custom for i8/i16, dropping to the correctly-sized
  // native store on the way out and masking/sign-extending in software
  // on the way back in, since READ8/READ16 leave the destination GPR's
  // upper bits genuinely undefined at the hardware level. See the
  // definitions in the .cpp.
  SDValue LowerSTORE(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerLOAD(SDValue Op, SelectionDAG &DAG) const;

  // Materialize a global/external symbol's address via LTHLISD::WRAPPER
  // -- see that enumerator's comment. Marked Custom for
  // ISD::GlobalAddress/ISD::ExternalSymbol in the constructor.
  SDValue LowerGlobalAddress(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerExternalSymbol(SDValue Op, SelectionDAG &DAG) const;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_LTHL_LTHLISELLOWERING_H
