//===-- LTHLAsmPrinter.cpp - LTHL LLVM assembly writer ----- ----*- C++ -*-===//
//
// Forward declarations shared across the LTHL codegen layer. As you add
// LTHLTargetMachine, LTHLISelDAGToDAG, LTHLAsmPrinter, etc., their
// creation functions (e.g. createLTHLISelDag(...)) get declared here so
// LTHLTargetMachine.cpp can wire up the pass pipeline without every file
// needing to include every other file's full header.
//
//===----------------------------------------------------------------------===//
// Converts MachineFunctions into LTHL assembly text (or, via the same
// MCStreamer path llvm-mc already exercises -- see notes_8 §5D's smoke
// test -- straight into an object file through LTHLMCCodeEmitter +
// LTHLAsmBackend).
//===----------------------------------------------------------------------===//

#include "LTHL.h"
#include "LTHLMCInstLower.h"
#include "LTHLTargetMachine.h"
#include "MCTargetDesc/LTHLInstPrinter.h"
#include "TargetInfo/LTHLTargetInfo.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/IR/Mangler.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "asm-printer"

namespace {

class LTHLAsmPrinter : public AsmPrinter {
public:
  LTHLAsmPrinter(TargetMachine &TM, std::unique_ptr<MCStreamer> Streamer)
      : AsmPrinter(TM, std::move(Streamer), ID) {}

  StringRef getPassName() const override { return "LTHL Assembly Printer"; }

  bool runOnMachineFunction(MachineFunction &MF) override;

  void emitInstruction(const MachineInstr *MI) override;

  void printOperand(const MachineInstr *MI, int OpNum, raw_ostream &O);

  bool PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                        const char *ExtraCode, raw_ostream &O) override;

  static char ID;
};

} // end anonymous namespace

void LTHLAsmPrinter::printOperand(const MachineInstr *MI, int OpNum,
                                   raw_ostream &O) {
  const MachineOperand &MO = MI->getOperand(OpNum);
  switch (MO.getType()) {
  default:
    llvm_unreachable("printOperand: unimplemented operand type");
  case MachineOperand::MO_Register:
    O << LTHLInstPrinter::getRegisterName(MO.getReg());
    return;
  case MachineOperand::MO_Immediate:
    // No '#' prefix -- LTHLAsmParser's syntax is bare immediates (see
    // LTHLInstPrinter::printOperand and notes_8 §3: "ld r30, 0x10000",
    // not "ld r30, #0x10000").
    O << MO.getImm();
    return;
  case MachineOperand::MO_MachineBasicBlock:
    MO.getMBB()->getSymbol()->print(O, MAI);
    return;
  case MachineOperand::MO_GlobalAddress: {
    uint64_t Offset = MO.getOffset();
    if (Offset)
      O << '(' << Offset << '+';
    getSymbol(MO.getGlobal())->print(O, MAI);
    if (Offset)
      O << ')';
    return;
  }
  }
}

// PrintAsmOperand - Print out an operand for an inline asm expression.
bool LTHLAsmPrinter::PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                                      const char *ExtraCode, raw_ostream &O) {
  if (ExtraCode && ExtraCode[0])
    return AsmPrinter::PrintAsmOperand(MI, OpNo, ExtraCode, O);

  printOperand(MI, OpNo, O);
  return false;
}

// PrintAsmMemoryOperand is deliberately NOT overridden: LTHL has no
// memory-operand kind at all (see notes_8 §3 -- READ/WRITE take a plain
// address *register*, not a base+offset memory operand), so there is
// nothing this backend could correctly print for an inline asm "m"
// constraint. AsmPrinter's base-class default already fails cleanly on
// that path; overriding it here to paper over the gap would be worse
// than leaving it alone.

static void lowerAndEmit(const MachineInstr *MI, MCContext &OutContext,
                          AsmPrinter &Printer, MCStreamer &OutStreamer) {
  LTHLMCInstLower MCInstLowering(OutContext, Printer);
  MCInst TmpInst;
  MCInstLowering.Lower(MI, TmpInst);
  Printer.EmitToStreamer(OutStreamer, TmpInst);
}

void LTHLAsmPrinter::emitInstruction(const MachineInstr *MI) {
  LTHL_MC::verifyInstructionPredicates(MI->getOpcode(),
                                        getSubtargetInfo().getFeatureBits());

  // NOTE: CALL_PSEUDO must never reach this function. It is expanded to
  // the real LD/ADDPC/CALLJ bundle by LTHLInstrInfo::expandPostRAPseudo
  // (see that function's comment for why the expansion has to happen
  // post-RA/post-scheduling, and why the three instructions are bundled
  // together via finalizeBundle). If this assert ever fires, the fix is
  // to make sure ExpandPostRAPseudos is actually running (check with
  // `llc -debug-pass=Structure` / `llc -stop-after=postrapseudos`).
  assert(MI->getOpcode() != LTHL::CALL_PSEUDO &&
         "CALL_PSEUDO reached AsmPrinter -- expandPostRAPseudo did not run");

  if (MI->isBundle()) {
    const MachineBasicBlock *MBB = MI->getParent();
    MachineBasicBlock::const_instr_iterator I = std::next(MI->getIterator());
    for (; I != MBB->instr_end() && I->isInsideBundle(); ++I) {
      if (I->isDebugInstr() || I->isImplicitDef())
        continue;
      lowerAndEmit(&*I, OutContext, *this, *OutStreamer);
    }
    return;
  }

  lowerAndEmit(MI, OutContext, *this, *OutStreamer);
}

bool LTHLAsmPrinter::runOnMachineFunction(MachineFunction &MF) {
  SetupMachineFunction(MF);
  emitFunctionBody();
  return false;
}

char LTHLAsmPrinter::ID = 0;

INITIALIZE_PASS(LTHLAsmPrinter, "lthl-asm-printer", "LTHL Assembly Printer",
                 false, false)

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeLTHLAsmPrinter() {
  RegisterAsmPrinter<LTHLAsmPrinter> X(getTheLTHLTarget());
}
