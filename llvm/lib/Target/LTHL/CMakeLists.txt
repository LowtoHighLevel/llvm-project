add_llvm_component_group(LTHL)

set(LLVM_TARGET_DEFINITIONS LTHL.td)

# TableGen generation.
tablegen(LLVM LTHLGenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM LTHLGenAsmMatcher.inc -gen-asm-matcher)
tablegen(LLVM LTHLGenRegisterInfo.inc -gen-register-info)
tablegen(LLVM LTHLGenInstrInfo.inc -gen-instr-info)
tablegen(LLVM LTHLGenSubtargetInfo.inc -gen-subtarget)
tablegen(LLVM LTHLGenCallingConv.inc -gen-callingconv)
tablegen(LLVM LTHLGenDAGISel.inc -gen-dag-isel)
tablegen(LLVM LTHLGenMCCodeEmitter.inc -gen-emitter)
tablegen(LLVM LTHLGenDisassemblerTables.inc -gen-disassembler)

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
add_subdirectory(Disassembler)
