add_llvm_component_library(LLVMLTHLAsmParser
  LTHLAsmParser.cpp

  LINK_COMPONENTS
  CodeGenTypes
  MC
  MCParser
  LTHLDesc
  LTHLInfo
  Support

  ADD_TO_COMPONENT
  LTHL
  )

target_include_directories(LLVMLTHLAsmParser PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR}/..
  ${CMAKE_CURRENT_SOURCE_DIR}/..
  )

add_dependencies(LLVMLTHLAsmParser LTHLCommonTableGen)
