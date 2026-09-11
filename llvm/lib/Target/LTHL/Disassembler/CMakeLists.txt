add_llvm_component_library(LLVMLTHLDisassembler
  LTHLDisassembler.cpp

  LINK_COMPONENTS
  MCDisassembler
  MC
  LTHLInfo
  Support

  ADD_TO_COMPONENT
  LTHL
  )

target_include_directories(LLVMLTHLDisassembler PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR}/..
  ${CMAKE_CURRENT_SOURCE_DIR}/..
  )

add_dependencies(LLVMLTHLDisassembler LTHLCommonTableGen)
