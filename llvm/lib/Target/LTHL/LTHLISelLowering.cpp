//===-- LTHLISelLowering.cpp - LTHL DAG Lowering Implementation ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LTHLISelLowering.h"
#include "LTHL.h"
#include "LTHLSubtarget.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define DEBUG_TYPE "lthl-lower"

LTHLTargetLowering::LTHLTargetLowering(const TargetMachine &TM,
                                        const LTHLSubtarget &STI)
    : TargetLowering(TM, STI) {
  // LTHL only has one usable register class.
  addRegisterClass(MVT::i32, &LTHL::GPRRegClass);
  computeRegisterProperties(STI.getRegisterInfo());

  setStackPointerRegisterToSaveRestore(LTHL::R30);
  setBooleanContents(ZeroOrOneBooleanContent);

  // No multiply/divide instructions exist in LTHLInstrInfo.td. Marking
  // these Expand (rather than leaving the default Legal, which would
  // send them straight to instruction selection with nothing able to
  // match them) routes them through SelectionDAGLegalize's automatic
  // libcall expansion instead -- __mulsi3, __udivsi3, etc.
  //
  for (unsigned Op : {ISD::MUL, ISD::MULHS, ISD::MULHU, ISD::UMUL_LOHI,
                       ISD::SMUL_LOHI, ISD::SDIV, ISD::UDIV, ISD::SREM,
                       ISD::UREM, ISD::SDIVREM, ISD::UDIVREM, ISD::ROTL,
                       ISD::ROTR})
    setOperationAction(Op, MVT::i32, Expand);

  setOperationAction(ISD::SETCC, MVT::i32, Custom);
  setOperationAction(ISD::SELECT_CC, MVT::i32, Custom);
  setOperationAction(ISD::SELECT, MVT::i32, Expand);

  setOperationAction(ISD::BR_CC, MVT::i32, Custom);
  setOperationAction(ISD::BRCOND, MVT::Other, Expand);

  // LTHL has no PC-relative or GOT-style addressing mode -- a symbol's
  // address has to be materialized via LD (simm24, resolved through
  // fixup_lthl_imm24) same as any other constant. See LowerGlobalAddress/
  // LowerExternalSymbol and the LTHLISD::WRAPPER comment in the .h.
  setOperationAction(ISD::GlobalAddress, MVT::i32, Custom);
  setOperationAction(ISD::ExternalSymbol, MVT::i32, Custom);

  for (MVT VT : {MVT::i8, MVT::i16}) {
    setTruncStoreAction(MVT::i32, VT, Custom);
    setLoadExtAction(ISD::ZEXTLOAD, MVT::i32, VT, Custom);
    setLoadExtAction(ISD::SEXTLOAD, MVT::i32, VT, Custom);
    setLoadExtAction(ISD::EXTLOAD, MVT::i32, VT, Custom);
  }

  setMinFunctionAlignment(Align(4));
  setPrefFunctionAlignment(Align(4));
}

const char *LTHLTargetLowering::getTargetNodeName(unsigned Opcode) const {
  switch (static_cast<LTHLISD::NodeType>(Opcode)) {
  case LTHLISD::CALL:
    return "LTHLISD::CALL";
  case LTHLISD::RET_GLUE:
    return "LTHLISD::RET_GLUE";
  case LTHLISD::BR_CC:
    return "LTHLISD::BR_CC";
  case LTHLISD::LOAD8:
    return "LTHLISD::LOAD8";
  case LTHLISD::LOAD16:
    return "LTHLISD::LOAD16";
  case LTHLISD::STORE8:
    return "LTHLISD::STORE8";
  case LTHLISD::STORE16:
    return "LTHLISD::STORE16";
  case LTHLISD::WRAPPER:
    return "LTHLISD::WRAPPER";
  case LTHLISD::SETCC:
    return "LTHLISD::SETCC";
  case LTHLISD::SELECT_CC:
    return "LTHLISD::SELECT_CC";

  }
  return nullptr;
}

SDValue LTHLTargetLowering::LowerOperation(SDValue Op,
                                            SelectionDAG &DAG) const {
  switch (Op.getOpcode()) {
  case ISD::BR_CC:
    return LowerBR_CC(Op, DAG);
  case ISD::SETCC:
    return LowerSETCC(Op, DAG);
  case ISD::SELECT_CC:
    return LowerSELECT_CC(Op, DAG);
  case ISD::STORE:
    return LowerSTORE(Op, DAG);
  case ISD::LOAD:
    return LowerLOAD(Op, DAG);
  case ISD::GlobalAddress:
    return LowerGlobalAddress(Op, DAG);
  case ISD::ExternalSymbol:
    return LowerExternalSymbol(Op, DAG);
  default:
    llvm_unreachable("Don't know how to custom lower this operation");
  }
}

SDValue LTHLTargetLowering::LowerLOAD(SDValue Op, SelectionDAG &DAG) const {
  auto *LD = cast<LoadSDNode>(Op);
  ISD::LoadExtType ExtType = LD->getExtensionType();
  assert(ExtType != ISD::NON_EXTLOAD &&
         "only extending loads are ever marked Custom");
  SDLoc DL(Op);
  EVT MemVT = LD->getMemoryVT(); // This is i8 or i16
  unsigned Bits = MemVT.getSizeInBits();

  SDValue Chain = LD->getChain();

  SDVTList VTs = DAG.getVTList(MVT::i32, MVT::Other);
  SDValue Ops[] = { Chain, LD->getBasePtr() };
  unsigned LoadOpc = (Bits == 8) ? LTHLISD::LOAD8 : LTHLISD::LOAD16;
  SDValue RawWord = DAG.getMemIntrinsicNode(
      LoadOpc, DL, VTs, Ops, MemVT,
      LD->getMemOperand());

  Chain = RawWord.getValue(1);

  uint32_t Mask = (Bits == 8) ? 0xFFu : 0xFFFFu;
  SDValue Masked = DAG.getNode(ISD::AND, DL, MVT::i32, RawWord,
                                DAG.getConstant(Mask, DL, MVT::i32));

  SDValue Result;
  if (ExtType == ISD::SEXTLOAD) {
    uint32_t SignBit = (Bits == 8) ? 0x80u : 0x8000u;
    SDValue Toggled = DAG.getNode(ISD::XOR, DL, MVT::i32, Masked,
                                   DAG.getConstant(SignBit, DL, MVT::i32));
    Result = DAG.getNode(ISD::SUB, DL, MVT::i32, Toggled,
                         DAG.getConstant(SignBit, DL, MVT::i32));
  } else {
    Result = Masked;
  }

  return DAG.getMergeValues({Result, Chain}, DL);
}

SDValue LTHLTargetLowering::LowerSTORE(SDValue Op, SelectionDAG &DAG) const {
  auto *ST = cast<StoreSDNode>(Op);
  assert(ST->isTruncatingStore() && "only truncating stores are ever marked Custom");
  SDLoc DL(Op);
  EVT MemVT = ST->getMemoryVT(); // Get original sub-word memory type (i8/i16)
  unsigned Bits = MemVT.getSizeInBits();

  SDValue Chain = ST->getChain();

  SDVTList VTs = DAG.getVTList(MVT::Other);
  SDValue Ops[] = { Chain, ST->getValue(), ST->getBasePtr() };
  unsigned StoreOpc = (Bits == 8) ? LTHLISD::STORE8 : LTHLISD::STORE16;
  return DAG.getMemIntrinsicNode(
      StoreOpc, DL, VTs, Ops, MemVT,
      ST->getMemOperand());
}

SDValue LTHLTargetLowering::LowerGlobalAddress(SDValue Op,
                                                SelectionDAG &DAG) const {
  const auto *GN = cast<GlobalAddressSDNode>(Op);
  SDLoc DL(Op);

  SDValue TGA =
      DAG.getTargetGlobalAddress(GN->getGlobal(), DL, MVT::i32, GN->getOffset());
  return DAG.getNode(LTHLISD::WRAPPER, DL, MVT::i32, TGA);
}

SDValue LTHLTargetLowering::LowerExternalSymbol(SDValue Op,
                                                 SelectionDAG &DAG) const {
  const auto *EN = cast<ExternalSymbolSDNode>(Op);
  SDLoc DL(Op);
  SDValue TES = DAG.getTargetExternalSymbol(EN->getSymbol(), MVT::i32);
  return DAG.getNode(LTHLISD::WRAPPER, DL, MVT::i32, TES);
}

SDValue LTHLTargetLowering::LowerBR_CC(SDValue Op, SelectionDAG &DAG) const {
  SDValue Chain = Op.getOperand(0);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(1))->get();
  SDValue LHS = Op.getOperand(2);
  SDValue RHS = Op.getOperand(3);
  SDValue Dest = Op.getOperand(4);
  SDLoc DL(Op);

  switch (CC) {
  case ISD::SETEQ:
  case ISD::SETNE:
  case ISD::SETLT:
  case ISD::SETLE:
  case ISD::SETGT:
  case ISD::SETGE:
  case ISD::SETULT:
  case ISD::SETULE:
  case ISD::SETUGT:
  case ISD::SETUGE:
    break;
  default:
    // Float/vector CondCodes (SETO, SETUO, SETFALSE2, ...) should never
    // reach here -- LTHL has no floating point and BR_CC's operands are
    // constrained to i32 by SDT_LTHLBrcc.
    report_fatal_error(
        "LTHL: unsupported CondCode reached LowerBR_CC");
  }

  // Selected 1:1 onto BR_CC_PSEUDO by the Pat in LTHLInstrInfo.td.
  return DAG.getNode(LTHLISD::BR_CC, DL, MVT::Other, Chain, LHS, RHS,
                      DAG.getTargetConstant(CC, DL, MVT::i32), Dest);
}

SDValue LTHLTargetLowering::LowerSETCC(SDValue Op, SelectionDAG &DAG) const {
  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(2))->get();
  SDLoc DL(Op);

  // Same CondCode set LowerBR_CC accepts -- emitSETCC's custom inserter
  // reuses the identical emitCondBranch helper emitBR_CC does, so
  // whatever's valid there is valid here too.
  switch (CC) {
  case ISD::SETEQ:
  case ISD::SETNE:
  case ISD::SETLT:
  case ISD::SETLE:
  case ISD::SETGT:
  case ISD::SETGE:
  case ISD::SETULT:
  case ISD::SETULE:
  case ISD::SETUGT:
  case ISD::SETUGE:
    break;
  default:
    report_fatal_error("LTHL: unsupported CondCode reached LowerSETCC");
  }

  // Selected 1:1 onto SETCC_PSEUDO by the Pat in LTHLInstrInfo.td.
  return DAG.getNode(LTHLISD::SETCC, DL, MVT::i32, LHS, RHS,
                      DAG.getTargetConstant(CC, DL, MVT::i32));
}

SDValue LTHLTargetLowering::LowerSELECT_CC(SDValue Op, SelectionDAG &DAG) const {
  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);
  SDValue True = Op.getOperand(2);
  SDValue False = Op.getOperand(3);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(4))->get();
  SDLoc DL(Op);

  switch (CC) {
  case ISD::SETEQ:
  case ISD::SETNE:
  case ISD::SETLT:
  case ISD::SETLE:
  case ISD::SETGT:
  case ISD::SETGE:
  case ISD::SETULT:
  case ISD::SETULE:
  case ISD::SETUGT:
  case ISD::SETUGE:
    break;
  default:
    report_fatal_error("LTHL: unsupported CondCode reached LowerSELECT_CC");
  }

  return DAG.getNode(LTHLISD::SELECT_CC, DL, Op.getValueType(), LHS, RHS,
                      True, False, DAG.getTargetConstant(CC, DL, MVT::i32));
}

//===----------------------------------------------------------------------===//
//                      Calling Convention Implementation
//===----------------------------------------------------------------------===//

#define GET_CALLING_CONV_IMPL
#include "LTHLGenCallingConv.inc"

SDValue LTHLTargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  if (IsVarArg)
    report_fatal_error("LTHL varargs functions are not supported yet");

  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MachineRegisterInfo &RegInfo = MF.getRegInfo();

  // R26 (the link register) holds this function's return address on
  // entry -- placed there by the caller's own CALL_PSEUDO expansion
  RegInfo.addLiveIn(LTHL::R26);

  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, ArgLocs, *DAG.getContext());
  CCInfo.AnalyzeFormalArguments(Ins, CC_LTHL);

  for (const CCValAssign &VA : ArgLocs) {
    if (VA.isRegLoc()) {
      // Argument arrives in a physical register -- copy it into a fresh
      // virtual register so the rest of the function can use it like any
      // other SSA value.
      Register VReg = RegInfo.createVirtualRegister(&LTHL::GPRRegClass);
      RegInfo.addLiveIn(VA.getLocReg(), VReg);
      InVals.push_back(DAG.getCopyFromReg(Chain, DL, VReg, VA.getLocVT()));
      continue;
    }

    assert(VA.isMemLoc() &&
           "argument must be passed in a register or on the stack");
    int FI =
        MFI.CreateFixedObject(4, VA.getLocMemOffset(), /*IsImmutable=*/true);
    SDValue FIN = DAG.getFrameIndex(FI, MVT::i32);
    InVals.push_back(
        DAG.getLoad(VA.getLocVT(), DL, Chain, FIN,
                     MachinePointerInfo::getFixedStack(MF, FI)));
  }

  return Chain;
}

bool LTHLTargetLowering::CanLowerReturn(
    CallingConv::ID CallConv, MachineFunction &MF, bool IsVarArg,
    const SmallVectorImpl<ISD::OutputArg> &Outs, LLVMContext &Context,
    const Type *RetTy) const {
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, RVLocs, Context);
  return CCInfo.CheckReturn(Outs, RetCC_LTHL);
}

SDValue LTHLTargetLowering::LowerReturn(
    SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::OutputArg> &Outs,
    const SmallVectorImpl<SDValue> &OutVals, const SDLoc &DL,
    SelectionDAG &DAG) const {
  if (IsVarArg)
    report_fatal_error("LTHL varargs functions are not supported yet");

  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, DAG.getMachineFunction(), RVLocs,
                 *DAG.getContext());
  CCInfo.AnalyzeReturn(Outs, RetCC_LTHL);

  SDValue Glue;
  SmallVector<SDValue, 4> RetOps(1, Chain);

  for (unsigned I = 0, E = RVLocs.size(); I != E; ++I) {
    const CCValAssign &VA = RVLocs[I];
    assert(VA.isRegLoc() && "RetCC_LTHL only assigns return values to registers");
    Chain = DAG.getCopyToReg(Chain, DL, VA.getLocReg(), OutVals[I], Glue);
    Glue = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
  }

  RetOps[0] = Chain;
  if (Glue.getNode())
    RetOps.push_back(Glue);

  // Selected 1:1 onto RET (jr r26) by the Pat in LTHLInstrInfo.td.
  return DAG.getNode(LTHLISD::RET_GLUE, DL, MVT::Other, RetOps);
}

SDValue LTHLTargetLowering::LowerCall(TargetLowering::CallLoweringInfo &CLI,
                                       SmallVectorImpl<SDValue> &InVals) const {
  SelectionDAG &DAG = CLI.DAG;
  SDLoc &DL = CLI.DL;
  SmallVectorImpl<ISD::OutputArg> &Outs = CLI.Outs;
  SmallVectorImpl<SDValue> &OutVals = CLI.OutVals;
  SmallVectorImpl<ISD::InputArg> &Ins = CLI.Ins;
  SDValue Chain = CLI.Chain;
  SDValue Callee = CLI.Callee;
  CallingConv::ID CallConv = CLI.CallConv;
  bool IsVarArg = CLI.IsVarArg;

  // LTHL has no return-address stack and R26 (the link register) isn't in
  // the CSR list in LTHLCallingConv.td, so a call clobbers the caller's
  // own return address -- nothing here attempts tail-call optimization.
  CLI.IsTailCall = false;

  if (IsVarArg)
    report_fatal_error("LTHL varargs calls are not supported yet");

  // Direct calls to a global function or external symbol: materialize
  // the symbol's address via LTHLISD::WRAPPER, then explicitly copy it 
  // into a register so that it always matches the register-callee path 
  // and selects your CALL_PSEUDO instruction pattern.
  if (auto *G = dyn_cast<GlobalAddressSDNode>(Callee)) {
    SDValue TGA = DAG.getTargetGlobalAddress(G->getGlobal(), DL, MVT::i32,
                                              G->getOffset());
    Callee = DAG.getNode(LTHLISD::WRAPPER, DL, MVT::i32, TGA);
    
    // Force the address into a register and update the chain dependency
    SDValue TargetReg = DAG.getTargetFrameIndex(0, MVT::i32); // Temporary placeholder if needed, usually empty copy suffices:
    Chain = DAG.getCopyToReg(Chain, DL, LTHL::R28, Callee); // Copying into R28 matching your parser's choice
    Callee = DAG.getCopyFromReg(Chain, DL, LTHL::R28, MVT::i32);
  } else if (auto *E = dyn_cast<ExternalSymbolSDNode>(Callee)) {
    SDValue TES = DAG.getTargetExternalSymbol(E->getSymbol(), MVT::i32);
    Callee = DAG.getNode(LTHLISD::WRAPPER, DL, MVT::i32, TES);
    
    // Force the address into a register and update the chain dependency
    Chain = DAG.getCopyToReg(Chain, DL, LTHL::R28, Callee);
    Callee = DAG.getCopyFromReg(Chain, DL, LTHL::R28, MVT::i32);
  }

  MachineFunction &MF = DAG.getMachineFunction();

  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, ArgLocs, *DAG.getContext());
  CCInfo.AnalyzeCallOperands(Outs, CC_LTHL);

  unsigned NumBytes = CCInfo.getStackSize();
  Chain = DAG.getCALLSEQ_START(Chain, NumBytes, 0, DL);

  SmallVector<std::pair<unsigned, SDValue>, 4> RegsToPass;
  SmallVector<SDValue, 4> MemOpChains;
  SDValue StackPtr;

  for (unsigned I = 0, E = ArgLocs.size(); I != E; ++I) {
    CCValAssign &VA = ArgLocs[I];
    SDValue Arg = OutVals[I];

    if (VA.isRegLoc()) {
      RegsToPass.emplace_back(VA.getLocReg(), Arg);
      continue;
    }

    assert(VA.isMemLoc() &&
           "argument must be passed in a register or on the stack");
    // LTHL reserves its call frame (no dedicated frame pointer -- see
    // LTHLFrameLowering::hasFPImpl), so outgoing stack args are written
    // straight to SP + offset rather than via ADJCALLSTACKDOWN/UP
    // pseudos.
    if (!StackPtr.getNode())
      StackPtr = DAG.getCopyFromReg(Chain, DL, LTHL::R30, MVT::i32);

    SDValue Addr =
        DAG.getNode(ISD::ADD, DL, MVT::i32, StackPtr,
                    DAG.getConstant(VA.getLocMemOffset(), DL, MVT::i32));
    MemOpChains.push_back(
        DAG.getStore(Chain, DL, Arg, Addr, MachinePointerInfo()));
  }

  if (!MemOpChains.empty())
    Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, MemOpChains);

  SDValue Glue;
  for (const auto &Reg : RegsToPass) {
    Chain = DAG.getCopyToReg(Chain, DL, Reg.first, Reg.second, Glue);
    Glue = Chain.getValue(1);
  }

  SmallVector<SDValue, 8> Ops;
  Ops.push_back(Chain);
  Ops.push_back(Callee);

  if (const uint32_t *Mask = MF.getSubtarget().getRegisterInfo()->getCallPreservedMask(
          MF, CallConv))
    Ops.push_back(DAG.getRegisterMask(Mask));

  // Add argument registers to the end of the list so they're known live
  // into the call.
  for (const auto &Reg : RegsToPass)
    Ops.push_back(DAG.getRegister(Reg.first, Reg.second.getValueType()));

  if (Glue.getNode())
    Ops.push_back(Glue);

  Chain = DAG.getNode(LTHLISD::CALL, DL, DAG.getVTList(MVT::Other, MVT::Glue),
                       Ops);
  Glue = Chain.getValue(1);

  Chain = DAG.getCALLSEQ_END(Chain, NumBytes, 0, Glue, DL);
  Glue = Chain.getValue(1);

  // Copy the return value(s) out of the physical registers RetCC_LTHL
  // assigned them to.
  SmallVector<CCValAssign, 16> RVLocs;
  CCState RetCCInfo(CallConv, IsVarArg, MF, RVLocs, *DAG.getContext());
  RetCCInfo.AnalyzeCallResult(Ins, RetCC_LTHL);

  for (const CCValAssign &VA : RVLocs) {
    SDValue RetVal =
        DAG.getCopyFromReg(Chain, DL, VA.getLocReg(), VA.getValVT(), Glue);
    Chain = RetVal.getValue(1);
    Glue = RetVal.getValue(2);
    InVals.push_back(RetVal);
  }

  return Chain;
}

//===----------------------------------------------------------------------===//
//                    Custom-Inserted Pseudo Instructions
//===----------------------------------------------------------------------===//

MachineBasicBlock *
LTHLTargetLowering::emitADDI(MachineInstr &MI, MachineBasicBlock *BB) const {
  const TargetInstrInfo &TII = *BB->getParent()->getSubtarget().getInstrInfo();
  MachineRegisterInfo &MRI = BB->getParent()->getRegInfo();
  DebugLoc DL = MI.getDebugLoc();

  Register Dst = MI.getOperand(0).getReg();
  Register Src = MI.getOperand(1).getReg();
  int64_t Imm = MI.getOperand(2).getImm();

  // LD <scratch>, Imm ; ADD Dst, Src, <scratch> -- see the comment on
  // ADDI in LTHLInstrInfo.td for why this needs two real instructions.
  // Unlike LTHLFrameLowering::adjustReg (which runs after register
  // allocation and has to clobber the globally-reserved R29), this runs
  // pre-RA, so a fresh virtual register works and the allocator handles
  // it normally.
  Register Scratch = MRI.createVirtualRegister(&LTHL::GPRRegClass);
  BuildMI(*BB, MI, DL, TII.get(LTHL::LD), Scratch).addImm(Imm);
  BuildMI(*BB, MI, DL, TII.get(LTHL::ADD), Dst).addReg(Src).addReg(Scratch);

  MI.eraseFromParent();
  return BB;
}

MachineBasicBlock *
LTHLTargetLowering::EmitInstrWithCustomInserter(MachineInstr &MI,
                                                 MachineBasicBlock *BB) const {
  switch (MI.getOpcode()) {
  case LTHL::ADDI:
    return emitADDI(MI, BB);
  case LTHL::BR_CC_PSEUDO:
    return emitBR_CC(MI, BB);
  case LTHL::SETCC_PSEUDO:
    return emitSETCC(MI, BB);
  case LTHL::SELECT_CC_PSEUDO:
    return emitSELECT_CC(MI, BB);
  case LTHL::SHL_PSEUDO:
    return emitShift(MI, BB, ShiftKind::SHL);
  case LTHL::SRL_PSEUDO:
    return emitShift(MI, BB, ShiftKind::SRL);
  case LTHL::SRA_PSEUDO:
    return emitShift(MI, BB, ShiftKind::SRA);
  default:
    llvm_unreachable("Unexpected instr type to insert");
  }
}

// Shared by emitBR_CC/emitSETCC -- see the declaration's comment in
// LTHLISelLowering.h. Builds the jr*/j* sequence for CC into BB,
// inserted immediately before MI, branching to Dest when the
// already-computed flags satisfy CC and to FalseSucc otherwise.
void LTHLTargetLowering::emitCondBranch(MachineBasicBlock *BB,
                                         MachineInstr &MI, ISD::CondCode CC,
                                         MachineBasicBlock *Dest,
                                         MachineBasicBlock *FalseSucc) const {
  MachineFunction *MF = BB->getParent();
  const TargetInstrInfo &TII = *MF->getSubtarget().getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();

  // See the flag-semantics comment on LTHLTargetLowering::LowerBR_CC for
  // where these combinations come from (confirmed against the emulator's
  // actual write_flag calls, not assumed).
  switch (CC) {
  case ISD::SETEQ:
    // LHS == RHS is exactly the Zero flag. Must still jump explicitly to
    // FalseSucc when not taken -- nothing guarantees FalseSucc is laid
    // out immediately after BB (e.g. when this fires from emitSETCC,
    // Dest/FalseSucc are freshly created TrueBB/FalseBB blocks whose
    // relative layout order is whatever MF->insert happened to use, not
    // necessarily "FalseSucc right after BB"). Relying on fallthrough
    // here left FalseSucc as a dangling, unreachable CFG successor --
    // see the "unexpected successors" MachineVerifier failure this fixed.
    BuildMI(*BB, MI, DL, TII.get(LTHL::JZ)).addMBB(Dest);
    BuildMI(*BB, MI, DL, TII.get(LTHL::J)).addMBB(FalseSucc);
    break;

  case ISD::SETNE:
    // No "branch if NOT zero" exists -- invert the sense by jumping
    // *around* an unconditional jump to Dest.
    BuildMI(*BB, MI, DL, TII.get(LTHL::JZ)).addMBB(FalseSucc);
    BuildMI(*BB, MI, DL, TII.get(LTHL::J)).addMBB(Dest);
    break;

  case ISD::SETUGE:
    // Carry=1 means no borrow, i.e. LHS >= RHS unsigned -- a single
    // Carry test suffices for the taken case, but (see SETEQ's comment
    // just above) the not-taken case still needs a real jump to
    // FalseSucc rather than assuming fallthrough.
    BuildMI(*BB, MI, DL, TII.get(LTHL::JC)).addMBB(Dest);
    BuildMI(*BB, MI, DL, TII.get(LTHL::J)).addMBB(FalseSucc);
    break;

  case ISD::SETULT:
    // LHS < RHS unsigned is exactly Carry=0 -- invert the same way SETNE
    // does, since there's no "branch if NOT carry".
    BuildMI(*BB, MI, DL, TII.get(LTHL::JC)).addMBB(FalseSucc);
    BuildMI(*BB, MI, DL, TII.get(LTHL::J)).addMBB(Dest);
    break;

  case ISD::SETULE:
    // True when Z=1 (equal) or C=0 (unsigned less-than); false only when
    // C=1 and Z=0.
    BuildMI(*BB, MI, DL, TII.get(LTHL::JZ)).addMBB(Dest);
    BuildMI(*BB, MI, DL, TII.get(LTHL::JC)).addMBB(FalseSucc);
    BuildMI(*BB, MI, DL, TII.get(LTHL::J)).addMBB(Dest);
    break;

  case ISD::SETUGT:
    // True only when C=1 and Z=0.
    BuildMI(*BB, MI, DL, TII.get(LTHL::JZ)).addMBB(FalseSucc);
    BuildMI(*BB, MI, DL, TII.get(LTHL::JC)).addMBB(Dest);
    BuildMI(*BB, MI, DL, TII.get(LTHL::J)).addMBB(FalseSucc);
    break;

  case ISD::SETLT:
  case ISD::SETLE:
  case ISD::SETGT:
  case ISD::SETGE: {
    // Signed comparisons hinge on Negative != Overflow (true for LT),
    // and LE/GT additionally gate on Zero first. Testing N!=V needs two
    // single-flag branches chained together -- LTHL has no instruction
    // that reads a flag into a GPR to XOR them arithmetically -- so this
    // needs a genuine extra basic block for the N=1 half of the decision
    // tree (the N=0 half stays inline in BB). Same "real jump, explicit
    // target" discipline as the loop emitShift builds below, just a
    // smaller two-way split instead of a loop.
    MachineBasicBlock *NSetBB = MF->CreateMachineBasicBlock(BB->getBasicBlock());
    MF->insert(++BB->getIterator(), NSetBB);
    BB->addSuccessor(NSetBB);
    NSetBB->addSuccessor(Dest);
    NSetBB->addSuccessor(FalseSucc);

    // LE/GT: peel off the Z=1 (equal) case up front, before the N/V
    // test -- LE is true on equal, GT is false on equal. What remains
    // after Z=0 is filtered out is exactly the LT test (for LE) or the
    // GE test (for GT).
    if (CC == ISD::SETLE)
      BuildMI(*BB, MI, DL, TII.get(LTHL::JZ)).addMBB(Dest);
    else if (CC == ISD::SETGT)
      BuildMI(*BB, MI, DL, TII.get(LTHL::JZ)).addMBB(FalseSucc);

    // DestOnNV: whether "N==V" (the GE condition) should branch to Dest.
    // True for GE and (post-Z-filter) GT; false for LT and (post-Z-filter)
    // LE, where "N==V" instead means the comparison is false.
    bool DestOnNV = (CC == ISD::SETGE || CC == ISD::SETGT);

    // N=0 path, still in BB: N==V here iff V=0, so JV tests the N!=V
    // (not-DestOnNV) case.
    BuildMI(*BB, MI, DL, TII.get(LTHL::JN)).addMBB(NSetBB);
    BuildMI(*BB, MI, DL, TII.get(LTHL::JV))
        .addMBB(DestOnNV ? FalseSucc : Dest);
    BuildMI(*BB, MI, DL, TII.get(LTHL::J)).addMBB(DestOnNV ? Dest : FalseSucc);

    // N=1 path, in NSetBB: N==V here iff V=1, so JV tests the N==V
    // (DestOnNV) case -- the mirror image of the N=0 path above.
    BuildMI(NSetBB, DL, TII.get(LTHL::JV)).addMBB(DestOnNV ? Dest : FalseSucc);
    BuildMI(NSetBB, DL, TII.get(LTHL::J)).addMBB(DestOnNV ? FalseSucc : Dest);
    break;
  }

  default:
    llvm_unreachable("LTHL: unsupported CondCode reached emitCondBranch -- "
                      "LowerBR_CC/LowerSETCC should have rejected it "
                      "earlier");
  }
}

MachineBasicBlock *
LTHLTargetLowering::emitBR_CC(MachineInstr &MI, MachineBasicBlock *BB) const {
  const TargetInstrInfo &TII = *BB->getParent()->getSubtarget().getInstrInfo();
  MachineRegisterInfo &MRI = BB->getParent()->getRegInfo();
  DebugLoc DL = MI.getDebugLoc();

  Register LHS = MI.getOperand(0).getReg();
  Register RHS = MI.getOperand(1).getReg();
  auto CC = static_cast<ISD::CondCode>(MI.getOperand(2).getImm());
  MachineBasicBlock *Dest = MI.getOperand(3).getMBB();

  // SUB sets FLAGS (Z/C/N/V) as a side effect; the actual subtraction
  // result is discarded into a scratch vreg -- only the flags matter
  // here. This mirrors emitADDI: pre-RA, so a fresh vreg is fine.
  Register Scratch = MRI.createVirtualRegister(&LTHL::GPRRegClass);
  BuildMI(*BB, MI, DL, TII.get(LTHL::SUB), Scratch).addReg(LHS).addReg(RHS);

  // BB's other CFG successor (the false edge ISel already wired up when
  // it built this block) is exactly where control should end up when the
  // comparison is false. Every branch built below is a real jump with an
  // explicit target, so none of this depends on final block layout/
  // fallthrough.
  MachineBasicBlock *FalseSucc = nullptr;
  for (MachineBasicBlock *Succ : BB->successors()) {
    if (Succ != Dest) {
      FalseSucc = Succ;
      break;
    }
  }
  assert(FalseSucc &&
         "BR_CC_PSEUDO's block must have a false-edge successor distinct "
         "from Dest");

  emitCondBranch(BB, MI, CC, Dest, FalseSucc);

  MI.eraseFromParent();
  return BB;
}

MachineBasicBlock *
LTHLTargetLowering::emitSETCC(MachineInstr &MI, MachineBasicBlock *BB) const {
  MachineFunction *MF = BB->getParent();
  MachineRegisterInfo &MRI = MF->getRegInfo();
  const TargetInstrInfo &TII = *MF->getSubtarget().getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();

  Register Dst = MI.getOperand(0).getReg();
  Register LHS = MI.getOperand(1).getReg();
  Register RHS = MI.getOperand(2).getReg();
  auto CC = static_cast<ISD::CondCode>(MI.getOperand(3).getImm());

  const BasicBlock *LLVM_BB = BB->getBasicBlock();
  MachineFunction::iterator InsertPt = ++BB->getIterator();

  MachineBasicBlock *TrueBB = MF->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *FalseBB = MF->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *ContBB = MF->CreateMachineBasicBlock(LLVM_BB);
  MF->insert(InsertPt, TrueBB);
  MF->insert(InsertPt, FalseBB);
  MF->insert(InsertPt, ContBB);

  // Move everything after the pseudo, plus BB's existing CFG edges and
  // any PHIs depending on them, into ContBB.
  ContBB->splice(ContBB->begin(), BB,
                  std::next(MachineBasicBlock::iterator(MI)), BB->end());
  ContBB->transferSuccessorsAndUpdatePHIs(BB);

  BB->addSuccessor(TrueBB);
  BB->addSuccessor(FalseBB);
  TrueBB->addSuccessor(ContBB);
  FalseBB->addSuccessor(ContBB);

  // SUB sets FLAGS; only its side effect matters here, same as
  // emitBR_CC.
  Register Scratch = MRI.createVirtualRegister(&LTHL::GPRRegClass);
  BuildMI(*BB, MI, DL, TII.get(LTHL::SUB), Scratch).addReg(LHS).addReg(RHS);

  // Reuse the exact same CondCode -> jr*/j* sequence BR_CC_PSEUDO uses,
  // just aimed at TrueBB/FalseBB instead of a branch target/fallthrough.
  emitCondBranch(BB, MI, CC, TrueBB, FalseBB);

  // TrueBB/FalseBB: materialize the boolean result in a fresh vreg each,
  // then rejoin -- real jumps to ContBB, not relying on fallthrough,
  // same discipline as emitCondBranch's own branches.
  Register TrueVal = MRI.createVirtualRegister(&LTHL::GPRRegClass);
  BuildMI(*TrueBB, TrueBB->end(), DL, TII.get(LTHL::LD), TrueVal).addImm(1);
  BuildMI(*TrueBB, TrueBB->end(), DL, TII.get(LTHL::J)).addMBB(ContBB);

  Register FalseVal = MRI.createVirtualRegister(&LTHL::GPRRegClass);
  BuildMI(*FalseBB, FalseBB->end(), DL, TII.get(LTHL::LD), FalseVal).addImm(0);
  BuildMI(*FalseBB, FalseBB->end(), DL, TII.get(LTHL::J)).addMBB(ContBB);

  BuildMI(*ContBB, ContBB->begin(), DL, TII.get(TargetOpcode::PHI), Dst)
      .addReg(TrueVal).addMBB(TrueBB)
      .addReg(FalseVal).addMBB(FalseBB);

  MI.eraseFromParent();
  return ContBB;
}

MachineBasicBlock *
LTHLTargetLowering::emitSELECT_CC(MachineInstr &MI,
                                   MachineBasicBlock *BB) const {
  MachineFunction *MF = BB->getParent();
  MachineRegisterInfo &MRI = MF->getRegInfo();
  const TargetInstrInfo &TII = *MF->getSubtarget().getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();

  Register Dst = MI.getOperand(0).getReg();
  Register LHS = MI.getOperand(1).getReg();
  Register RHS = MI.getOperand(2).getReg();
  Register TVal = MI.getOperand(3).getReg();
  Register FVal = MI.getOperand(4).getReg();
  auto CC = static_cast<ISD::CondCode>(MI.getOperand(5).getImm());

  const BasicBlock *LLVM_BB = BB->getBasicBlock();
  MachineFunction::iterator InsertPt = ++BB->getIterator();

  MachineBasicBlock *TrueBB = MF->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *FalseBB = MF->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *ContBB = MF->CreateMachineBasicBlock(LLVM_BB);
  MF->insert(InsertPt, TrueBB);
  MF->insert(InsertPt, FalseBB);
  MF->insert(InsertPt, ContBB);

  // Move everything after the pseudo, plus BB's existing CFG edges and
  // any PHIs depending on them, into ContBB -- same splice idiom
  // emitSETCC uses.
  ContBB->splice(ContBB->begin(), BB,
                  std::next(MachineBasicBlock::iterator(MI)), BB->end());
  ContBB->transferSuccessorsAndUpdatePHIs(BB);

  BB->addSuccessor(TrueBB);
  BB->addSuccessor(FalseBB);
  TrueBB->addSuccessor(ContBB);
  FalseBB->addSuccessor(ContBB);

  // SUB sets FLAGS; only its side effect matters here, same as
  // emitBR_CC/emitSETCC.
  Register Scratch = MRI.createVirtualRegister(&LTHL::GPRRegClass);
  BuildMI(*BB, MI, DL, TII.get(LTHL::SUB), Scratch).addReg(LHS).addReg(RHS);

  // Reuse the exact same CondCode -> jr*/j* sequence BR_CC_PSEUDO/
  // SETCC_PSEUDO use, aimed at TrueBB/FalseBB.
  emitCondBranch(BB, MI, CC, TrueBB, FalseBB);

  // TrueBB/FalseBB: nothing to compute -- $tval/$fval already exist as
  // GPRs -- just a real jump to ContBB, same discipline as
  // emitCondBranch's own branches (no reliance on fallthrough).
  BuildMI(*TrueBB, TrueBB->end(), DL, TII.get(LTHL::J)).addMBB(ContBB);
  BuildMI(*FalseBB, FalseBB->end(), DL, TII.get(LTHL::J)).addMBB(ContBB);

  BuildMI(*ContBB, ContBB->begin(), DL, TII.get(TargetOpcode::PHI), Dst)
      .addReg(TVal).addMBB(TrueBB)
      .addReg(FVal).addMBB(FalseBB);

  MI.eraseFromParent();
  return ContBB;
}

// Builds a runtime loop for SHL_PSEUDO/SRL_PSEUDO/SRA_PSEUDO -- LTHL has
// no shift instruction, only 1-bit rotate-through-carry, so a shift by
// an arbitrary amount has to be done one bit at a time. Structurally
// this is the standard "count-down while loop" custom-inserter shape
// (same idea MSP430's shift expansion uses, for the same reason: no
// barrel shifter there either) -- three blocks:
//   BB (the pseudo's original block, split here):
//     test amt == 0 up front and skip the loop entirely if so
//   LoopBB:
//     PHI-join the running value/remaining-amount, do one bit of shift,
//     decrement, test, branch back or fall out to ExitBB
//   ExitBB:
//     PHI-join the result (either the untouched original value, if the
//     zero-amount fast path was taken, or the loop's final value)
// Every branch here is a real jump with an explicit target (JZ/J), same
// as emitBR_CC above -- nothing depends on block layout or fallthrough.
MachineBasicBlock *LTHLTargetLowering::emitShift(MachineInstr &MI,
                                                  MachineBasicBlock *BB,
                                                  ShiftKind Kind) const {
  MachineFunction *MF = BB->getParent();
  MachineRegisterInfo &MRI = MF->getRegInfo();
  const TargetInstrInfo &TII = *MF->getSubtarget().getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();

  Register DstReg = MI.getOperand(0).getReg();
  Register SrcReg = MI.getOperand(1).getReg();
  Register AmtReg = MI.getOperand(2).getReg();

  const BasicBlock *LLVM_BB = BB->getBasicBlock();
  MachineFunction::iterator InsertPt = ++BB->getIterator();

  MachineBasicBlock *LoopBB = MF->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *ExitBB = MF->CreateMachineBasicBlock(LLVM_BB);
  MF->insert(InsertPt, LoopBB);
  MF->insert(InsertPt, ExitBB);

  // Move everything after the pseudo (and BB's existing successors) into
  // ExitBB -- this needs an explicit block split, unlike emitADDI/
  // emitBR_CC's single BuildMI(*BB, MI, ...) insertion point, since this
  // is a real 3-block loop rather than a same-block instruction swap.
  ExitBB->splice(ExitBB->begin(), BB,
                  std::next(MachineBasicBlock::iterator(MI)), BB->end());
  ExitBB->transferSuccessorsAndUpdatePHIs(BB);

  BB->addSuccessor(ExitBB);
  BB->addSuccessor(LoopBB);
  LoopBB->addSuccessor(ExitBB);
  LoopBB->addSuccessor(LoopBB);

  // BB: skip the loop entirely if the shift amount is already zero.
  //   sub scratch, amt, r0   -- just to set FLAGS.Zero from amt
  //   jz ExitBB
  Register ZeroTestScratch = MRI.createVirtualRegister(&LTHL::GPRRegClass);
  BuildMI(BB, DL, TII.get(LTHL::SUB), ZeroTestScratch)
      .addReg(AmtReg).addReg(LTHL::R0);
  BuildMI(BB, DL, TII.get(LTHL::JZ)).addMBB(ExitBB);

  // Loop-invariant constant (the per-iteration decrement), materialized
  // once in BB rather than inside LoopBB.
  Register One = MRI.createVirtualRegister(&LTHL::GPRRegClass);
  BuildMI(BB, DL, TII.get(LTHL::LD), One).addImm(1);

  // LoopBB:
  //   Val = phi [SrcReg, BB], [NextVal, LoopBB]
  //   Amt = phi [AmtReg, BB], [NextAmt, LoopBB]
  //   <one bit of shift: Val -> NextVal, per Kind -- see the comment on
  //    SHL_PSEUDO/SRL_PSEUDO/SRA_PSEUDO in LTHLInstrInfo.td>
  //   NextAmt = sub Amt, One
  //   scratch2 = sub NextAmt, r0   ; test NextAmt == 0
  //   jz ExitBB
  //   j LoopBB
  Register Val = MRI.createVirtualRegister(&LTHL::GPRRegClass);
  Register NextVal = MRI.createVirtualRegister(&LTHL::GPRRegClass);
  Register Amt = MRI.createVirtualRegister(&LTHL::GPRRegClass);
  Register NextAmt = MRI.createVirtualRegister(&LTHL::GPRRegClass);

  BuildMI(LoopBB, DL, TII.get(TargetOpcode::PHI), Val)
      .addReg(SrcReg).addMBB(BB)
      .addReg(NextVal).addMBB(LoopBB);
  BuildMI(LoopBB, DL, TII.get(TargetOpcode::PHI), Amt)
      .addReg(AmtReg).addMBB(BB)
      .addReg(NextAmt).addMBB(LoopBB);

  switch (Kind) {
  case ShiftKind::SHL:
    // NextVal = Val + Val -- doubling; mod-2^32 wraparound is exactly a
    // left shift by one.
    BuildMI(LoopBB, DL, TII.get(LTHL::ADD), NextVal).addReg(Val).addReg(Val);
    break;
  case ShiftKind::SRL: {
    // Clear Carry (0+0 always carries out 0), then rotate Val through
    // it: the cleared carry fills the vacated top bit with 0. rs2 is
    // unused by ROR (see its comment in LTHLInstrInfo.td) -- r0 for both
    // the clearing add and the rotate's unused operand, same idiom as
    // `mov`.
    Register CarryClearScratch =
        MRI.createVirtualRegister(&LTHL::GPRRegClass);
    BuildMI(LoopBB, DL, TII.get(LTHL::ADD), CarryClearScratch)
        .addReg(LTHL::R0).addReg(LTHL::R0);
    BuildMI(LoopBB, DL, TII.get(LTHL::ROR), NextVal)
        .addReg(Val).addReg(LTHL::R0);
    break;
  }
  case ShiftKind::SRA: {
    // Self-add Val (discarding the doubled result -- only its Carry
    // out matters, which equals Val's original MSB), then rotate Val
    // itself (not the discarded doubled scratch) through that carry:
    // the saved sign bit rotates back in as the new top bit.
    Register SignScratch = MRI.createVirtualRegister(&LTHL::GPRRegClass);
    BuildMI(LoopBB, DL, TII.get(LTHL::ADD), SignScratch)
        .addReg(Val).addReg(Val);
    BuildMI(LoopBB, DL, TII.get(LTHL::ROR), NextVal)
        .addReg(Val).addReg(LTHL::R0);
    break;
  }
  }

  BuildMI(LoopBB, DL, TII.get(LTHL::SUB), NextAmt).addReg(Amt).addReg(One);

  Register LoopZeroTestScratch = MRI.createVirtualRegister(&LTHL::GPRRegClass);
  BuildMI(LoopBB, DL, TII.get(LTHL::SUB), LoopZeroTestScratch)
      .addReg(NextAmt).addReg(LTHL::R0);
  BuildMI(LoopBB, DL, TII.get(LTHL::JZ)).addMBB(ExitBB);
  BuildMI(LoopBB, DL, TII.get(LTHL::J)).addMBB(LoopBB);

  // ExitBB: DstReg = phi [SrcReg, BB (zero-amount fast path)],
  //                      [NextVal, LoopBB (loop's final value)]
  BuildMI(*ExitBB, ExitBB->begin(), DL, TII.get(TargetOpcode::PHI), DstReg)
      .addReg(SrcReg).addMBB(BB)
      .addReg(NextVal).addMBB(LoopBB);

  MI.eraseFromParent();
  return ExitBB;
}
