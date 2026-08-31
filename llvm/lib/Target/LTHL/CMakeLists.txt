add_llvm_component_group(LTHL)

set(LLVM_TARGET_DEFINITIONS LTHL.td)

# TableGen generation. Add more tblgen_add_target_gen lines here as you add
# a Disassembler (-gen-disassembler) in a later step.
tablegen(LLVM LTHLGenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM LTHLGenAsmMatcher.inc -gen-asm-matcher)
tablegen(LLVM LTHLGenRegisterInfo.inc -gen-register-info)
tablegen(LLVM LTHLGenInstrInfo.inc -gen-instr-info)
tablegen(LLVM LTHLGenSubtargetInfo.inc -gen-subtarget)
tablegen(LLVM LTHLGenCallingConv.inc -gen-callingconv)
tablegen(LLVM LTHLGenDAGISel.inc -gen-dag-isel)
tablegen(LLVM LTHLGenMCCodeEmitter.inc -gen-emitter)

add_public_tablegen_target(LTHLCommonTableGen)

add_llvm_target(LTHLCodeGen
  LTHL.h
  LTHLAsmPrinter.cpp
  LTHLFrameLowering.cpp
  LTHLInstrInfo.cpp
  LTHLISelDAGToDAG.cpp
  LTHLMCInstLower.cpp
  LTHLRegisterInfo.cpp
  LTHLSelectionDAGInfo.cpp
  LTHLSubtarget.cpp
  LTHLISelLowering.cpp
  LTHLTargetMachine.cpp

  LINK_COMPONENTS
  AsmPrinter
  Core
  CodeGen
  MC
  LTHLDesc
  LTHLInfo
  Support
  Target

  ADD_TO_COMPONENT
  LTHL
  )

add_subdirectory(TargetInfo)
add_subdirectory(MCTargetDesc)
add_subdirectory(AsmParser)

# Uncomment once you add this directory in a later step:
# add_subdirectory(Disassembler)
