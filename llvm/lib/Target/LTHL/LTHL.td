//===-- LTHL.td - Top-level Tablegen for LTHL ---------------*- Tablegen -*-===//
//
// Forward declarations shared across the LTHL codegen layer. As you add
// LTHLTargetMachine, LTHLISelDAGToDAG, LTHLAsmPrinter, etc., their
// creation functions (e.g. createLTHLISelDag(...)) get declared here so
// LTHLTargetMachine.cpp can wire up the pass pipeline without every file
// needing to include every other file's full header.
//
//===----------------------------------------------------------------------===//
include "llvm/Target/Target.td"

def FeatureInt
    : SubtargetFeature<"int", "HasInt", "true",
                        "Enable hardware interrupts">;

include "LTHLRegisterInfo.td"
include "LTHLCallingConv.td"
include "LTHLInstrInfo.td"

def LTHLInstrInfo : InstrInfo;

def : ProcessorModel<"generic", NoSchedModel, []>;
def : ProcessorModel<"generic-int", NoSchedModel, [FeatureInt]>;


def LTHLAsmWriter : AsmWriter {
  string AsmWriterClassName = "InstPrinter";
  bit isMCAsmWriter = 1;
}

def LTHLAsmParser : AsmParser {
  let ShouldEmitMatchRegisterName = 1;
}

def LTHL : Target {
  let InstructionSet = LTHLInstrInfo;
  let AssemblyWriters = [LTHLAsmWriter];
  let AssemblyParsers = [LTHLAsmParser];
  let AllowRegisterRenaming = 1;
}
