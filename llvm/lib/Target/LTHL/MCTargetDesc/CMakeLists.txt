add_llvm_component_library(LLVMLTHLDesc
  LTHLAsmBackend.cpp
  LTHLELFObjectWriter.cpp
  LTHLInstPrinter.cpp
  LTHLMCAsmInfo.cpp
  LTHLMCCodeEmitter.cpp
  LTHLMCTargetDesc.cpp

  LINK_COMPONENTS
  MC
  Support
  LTHLInfo

  ADD_TO_COMPONENT
  LTHL
  )

target_include_directories(LLVMLTHLDesc PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR}/..
  ${CMAKE_CURRENT_SOURCE_DIR}/..
  )

add_dependencies(LLVMLTHLDesc LTHLCommonTableGen)
