add_llvm_component_library(LLVMLTHLInfo
  LTHLTargetInfo.cpp

  LINK_COMPONENTS
  Support

  ADD_TO_COMPONENT
  LTHL
  )
