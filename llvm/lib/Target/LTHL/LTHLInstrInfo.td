

// Custom match class for LD's immediate: plain isImm() (the default the
// AsmMatcher would otherwise auto-generate) accepts anything, but a
// literal constant that doesn't fit in 24 signed bits should be a real
// parse-time diagnostic rather than silently truncated bits. A symbolic
// expression (e.g. `ld r5, some_label` to materialize an address) isn't
// something we can range-check yet -- there's no relocation support
// until MCCodeEmitter exists -- so those are accepted unconditionally
// and left for a later pass to validate.
def SImm24AsmOperand : AsmOperandClass {
  let Name = "SImm24";
  let PredicateMethod = "isSImm24";
  let RenderMethod = "addImmOperands";
  let DiagnosticString = "immediate must fit in a signed 24-bit integer";
}

def simm24 : Operand<i32> {
  let DecoderMethod = "decodeSImmOperand<24>";
  let ParserMatchClass = SImm24AsmOperand;
}

def simm16 : Operand<i32> {
  let DecoderMethod = "decodeSImmOperand<16>";
}

// PC-relative branch target, stored as a signed word-aligned offset.
def brtarget : Operand<OtherVT> {
  let EncoderMethod = "getBranchTargetOpValue";
  let DecoderMethod  = "decodeBranchTarget";
  let OperandType = "OPERAND_PCREL";
}

// Absolute jump target for call/jump instructions.
def calltarget : Operand<i32> {
  let EncoderMethod = "getCallTargetOpValue";
}

// Tell TableGen how your target implements the standard stack guard pseudo
def LTHL_LOAD_STACK_GUARD : TargetSpecializedStandardPseudoInstruction<LOAD_STACK_GUARD> {
  let Namespace = "LTHL";
  // Replace the generic ptr_rc output with your target's concrete register class
  let OutOperandList = (outs GPR:$dst); 
}

def LTHL_PREALLOCATED_ARG : TargetSpecializedStandardPseudoInstruction<PREALLOCATED_ARG> {
  let Namespace = "LTHL";
  let OutOperandList = (outs GPR:$dst);
}

def LTHL_PATCHABLE_EVENT_CALL : TargetSpecializedStandardPseudoInstruction<PATCHABLE_EVENT_CALL> {
  let Namespace = "LTHL";
  let InOperandList = (ins GPR:$src1, GPR:$src2); // Adjust operand constraints to match GPR width
}

def LTHL_PATCHABLE_TYPED_EVENT_CALL : TargetSpecializedStandardPseudoInstruction<PATCHABLE_TYPED_EVENT_CALL> {
  let Namespace = "LTHL";
  let InOperandList = (ins GPR:$src1, GPR:$src2, GPR:$src3);
}

//===----------------------------------------------------------------------===//
// Base instruction class: every YourArch instruction is exactly 32 bits.
//===----------------------------------------------------------------------===//

class LTHLInst<bits<3> opc, dag outs, dag ins, string asmstr, list<dag> pattern>
    : Instruction {
  let Namespace = "LTHL";
  field bits<32> Inst;

  bits<3> Opcode = opc;
  let Inst{31-29} = Opcode;

  let OutOperandList = outs;
  let InOperandList  = ins;
  let AsmString      = asmstr;
  let Pattern        = pattern;
  let Size = 4; // bytes — always 4, fixed length
}


class FormR<bits<3> opc, bits<5> funct, dag outs, dag ins, string asmstr,
            list<dag> pattern>
    : LTHLInst<opc, outs, ins, asmstr, pattern> {
  bits<5> rd;
  bits<5> rs1;
  bits<5> rs2;

  // Bit layout updated to match arch-base.md's corrected R-Type row:
  // `000 NEGB CARRYIN aluop areg breg outreg` (was `... outreg areg
  // breg`) -- Cory found an emulator bug and fixed the doc accordingly.
  // rd/rs1/rs2 keep their existing semantic bindings (rd=outreg,
  // rs1=areg, rs2=breg -- see e.g. ADD's "add $rd, $rs1, $rs2" already
  // meaning outreg = areg + breg), only *which bit slot* each field
  // lands in has moved: rs1(areg) now occupies the first register slot,
  // rs2(breg) the second, rd(outreg) the third (last).
  let Inst{28-24} = funct;
  let Inst{23-21} = 0;
  let Inst{20-16} = rs1;
  let Inst{15-13} = 0;
  let Inst{12-8} = rs2;
  let Inst{7-5} = 0;
  let Inst{4-0} = rd;
}

class FormI<bits<3> opc, dag outs, dag ins, string asmstr,
            list<dag> pattern>
    : LTHLInst<opc, outs, ins, asmstr, pattern> {
  bits<5> rd;
  bits<24> imm;

  let Inst{28-24} = rd;
  let Inst{23-0} = imm;
}


// WIDTH occupies the low two bits of the instruction's most-significant
// byte (Inst{25-24}) -- directly below the 3-bit opcode, inside what
// used to be an all-zero Inst{28-21} block on both FormMR and FormMW.
// 00 = 32-bit (word): bit-for-bit identical to every plain read/write
// encoded before this field existed, so previously-assembled 32-bit-only
// object code is unaffected. 01 = 16-bit (halfword). 10 = 8-bit (byte).
// 11 is currently undefined/reserved.
class MemWidth<string suffix, bits<2> val> {
  string Suffix = suffix;
  bits<2> Value = val;
}

def WIDTH_32 : MemWidth<"",   0b00>;
def WIDTH_16 : MemWidth<"16", 0b01>;
def WIDTH_8  : MemWidth<"8",  0b10>;

class FormMR<bits<3> opc, bits<2> width, dag outs, dag ins, string asmstr,
            list<dag> pattern>
    : LTHLInst<opc, outs, ins, asmstr, pattern> {
  bits<5> addr;
  bits<5> data;

  let Inst{28-26} = 0;
  let Inst{25-24} = width;
  let Inst{23-21} = 0;
  let Inst{20-16} = addr;
  let Inst{15-5} = 0;
  let Inst{4-0} = data;
}

class FormMW<bits<3> opc, bits<2> width, dag outs, dag ins, string asmstr,
            list<dag> pattern>
    : LTHLInst<opc, outs, ins, asmstr, pattern> {
  bits<5> addr;
  bits<5> data;

  let Inst{28-26} = 0;
  let Inst{25-24} = width;
  let Inst{23-21} = 0;
  let Inst{20-16} = addr;
  let Inst{15-13} = 0;
  let Inst{12-8} = data;
  let Inst{7-0} = 0;
}

class FormJRL<bits<3> opc, dag outs, dag ins, string asmstr,
            list<dag> pattern>
    : LTHLInst<opc, outs, ins, asmstr, pattern> {
  bits<5> cond;
  bits<16> rel;

  let Inst{28-24} = cond;
  let Inst{23-16} = 0;
  let Inst{15-0} = rel;
}

class FormJRG<bits<3> opc, bits<5> cond, dag outs, dag ins, string asmstr,
            list<dag> pattern>
    : LTHLInst<opc, outs, ins, asmstr, pattern> {
  // Named `target` (not `reg`) to match every instruction built from
  // this class -- JR/JRZ/JRC/JRV/JRN/CALLJ all fill this field via a
  // real `(ins GPR:$target)` operand, and RET overrides it statically
  // (`let target = 26`, see below) instead of binding it to an operand
  // at all. TableGen's implicit Inst-bit-field-to-operand binding
  // matches by name, same as every other Form* class here (FormR's
  // rd/rs1/rs2, FormMR/FormMW's addr/data, FormJRL's rel) -- this field
  // was originally named `reg`, which happened to compile fine for
  // -gen-instr-info/-gen-asm-writer/-gen-asm-matcher/-gen-dag-isel, but
  // -gen-emitter's CodeEmitterGen is the first backend that actually
  // needs to resolve the binding, and errors ("No operand named reg in
  // record CALLJ") the moment it tries.
  bits<5> target;

  let Inst{28-24} = cond;
  let Inst{23-21} = 0;
  let Inst{20-16} = target;
  let Inst{15-0} = 0;
}


// A separate, lightweight base for pseudo instructions — no real Inst
// encoding required, since these never reach the MCCodeEmitter.
class LTHLPseudo<dag outs, dag ins, string asmstr, list<dag> pattern>
    : Instruction {
  let Namespace = "LTHL";
  let OutOperandList = outs;
  let InOperandList  = ins;
  let AsmString      = asmstr;
  let Pattern        = pattern;
  let isPseudo = 1;
  let isCodeGenOnly = 1;
}

//===----------------------------------------------------------------------===//
// Custom SelectionDAG nodes used only by LTHLISelLowering's LowerCall/
// LowerReturn (see LTHLISelLowering.h's LTHLISD::NodeType enum). These
// exist purely so the Pat rules below can select them straight onto the
// instruction that implements them -- LTHLISelDAGToDAG won't need any
// custom C++ for either once it exists, since table-driven pattern
// matching handles it.
//===----------------------------------------------------------------------===//

// Sub-word memory access nodes -- selected straight onto READ8/READ16/
// WRITE8/WRITE16 (see the Pat rules right after those defs, below).
// Replaces the old lthl_word_load/lthl_word_store word-per-variable
// model: those selected onto plain READ/WRITE (always a full 32-bit
// transfer), with masking/sign-extension done entirely in software on
// the load side -- correct for an isolated scalar, but WRITE's
// full-word write silently clobbered whatever else lived in the other
// 16/24 bits of that word. That's exactly the stack-corruption bug this
// hardware change (native 8/16-bit READ/WRITE, see MemWidth in
// LTHLInstrInfo.td) fixes. READ8/READ16 leave the upper bits of the
// destination GPR genuinely undefined at the hardware level (confirmed,
// not assumed) -- LowerLOAD's existing AND-mask, and for
// sign-extending loads the XOR/SUB sign-extension trick, still has to
// run in software exactly as before; only the memory-side access width
// changed from always-32-bit to the real 8/16-bit transfer, which is
// what stops the clobbering.
def lthl_load8 : SDNode<"LTHLISD::LOAD8", SDTLoad,
                        [SDNPHasChain, SDNPMayLoad, SDNPMemOperand]>;
def lthl_load16 : SDNode<"LTHLISD::LOAD16", SDTLoad,
                         [SDNPHasChain, SDNPMayLoad, SDNPMemOperand]>;
def lthl_store8 : SDNode<"LTHLISD::STORE8", SDTStore,
                         [SDNPHasChain, SDNPMayStore, SDNPMemOperand]>;
def lthl_store16 : SDNode<"LTHLISD::STORE16", SDTStore,
                          [SDNPHasChain, SDNPMayStore, SDNPMemOperand]>;

def SDT_LTHLCall : SDTypeProfile<0, -1, [SDTCisVT<0, i32>]>;
def LTHLcall : SDNode<"LTHLISD::CALL", SDT_LTHLCall,
                       [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue,
                        SDNPVariadic]>;

def LTHLretglue : SDNode<"LTHLISD::RET_GLUE", SDTNone,
                          [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;

// Wraps a TargetGlobalAddress/TargetExternalSymbol so it can be selected
// straight onto LD (see the Pat rules right below LD's def) -- LTHL has
// no PC-relative or GOT-style addressing, so a symbol's address is
// materialized exactly like any other 24-bit constant, via LD's simm24
// field and fixup_lthl_imm24 (see LTHLFixupKinds.h and
// LTHLMCCodeEmitter.cpp's getMachineOpValue, which already routes any
// non-constant expression operand through that fixup). Built by
// LTHLTargetLowering::LowerGlobalAddress/LowerExternalSymbol and by
// LowerCall for direct calls to a global/external symbol.
def SDT_LTHLWrapper : SDTypeProfile<1, 1, [SDTCisSameAs<0, 1>, SDTCisVT<0, i32>]>;
def LTHLWrapper : SDNode<"LTHLISD::WRAPPER", SDT_LTHLWrapper>;

// Concrete instructions
let isCommutable = 1, Defs = [FLAGS] in
def ADD : FormR<0x0, 0x0, (outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2),
                "add $rd, $rs1, $rs2",
                [(set GPR:$rd, (add GPR:$rs1, GPR:$rs2))]>;

// mov $rd, $rs -- pure assembler convenience, not a distinct opcode: r0
// always reads as zero (see the ZERO reg class in LTHLRegisterInfo.td),
// so `mov rd, rs` encodes identically to, and behaves exactly like,
// `add rd, rs, r0` -- including setting FLAGS as ADD's side effect.
def : InstAlias<"mov $rd, $rs", (ADD GPR:$rd, GPR:$rs, R0)>;

let Defs = [FLAGS] in
def SUB : FormR<0x0, 0b11000, (outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2),
                "sub $rd, $rs1, $rs2",
                [(set GPR:$rd, (sub GPR:$rs1, GPR:$rs2))]>;

let isCommutable = 1, Defs = [FLAGS] in
def AND : FormR<0x0, 0b00001, (outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2),
                "and $rd, $rs1, $rs2",
                [(set GPR:$rd, (and GPR:$rs1, GPR:$rs2))]>;

let isCommutable = 1, Defs = [FLAGS] in
def OR : FormR<0x0, 0b00010, (outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2),
               "or $rd, $rs1, $rs2",
               [(set GPR:$rd, (or GPR:$rs1, GPR:$rs2))]>;

let isCommutable = 1, Defs = [FLAGS] in
def XOR : FormR<0x0, 0b00011, (outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2),
                "xor $rd, $rs1, $rs2",
                [(set GPR:$rd, (xor GPR:$rs1, GPR:$rs2))]>;

// ROR/ROL rotate *through* the Carry flag (see the doc's ALU Operations
// table: lowest bit -> Carry, Carry -> highest bit for ROR, and the
// mirror image for ROL) -- so unlike XOR they both read and clobber
// FLAGS. LLVM's rotl/rotr ISD nodes carry no such flag dependency, so
// there is no safe Pat to attach here (a real one would need a custom
// SDNode threading FLAGS through explicitly, the same way BR_CC does).
// These are assembler/encoding-only for now -- not selectable from IR.
// breg is still a real operand, since every R-type ALU op reads both
// ALU inputs per the doc's R-Type format, even though rotate only uses
// rs1; asm syntax keeps it explicit rather than silently defaulting it.
let Uses = [FLAGS], Defs = [FLAGS] in {
def ROR : FormR<0x0, 0b00100, (outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2),
                "ror $rd, $rs1, $rs2", []>;

def ROL : FormR<0x0, 0b00101, (outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2),
                "rol $rd, $rs1, $rs2", []>;
}

def LD : FormI<0x1, (outs GPR:$rd), (ins simm24:$imm),
                  "ld $rd, $imm",
                  [(set GPR:$rd, imm:$imm)]>;

// Materialize a global/external symbol's address via LD -- see
// LTHLWrapper's comment above. simm24 only needs to accept these as an
// *output*-pattern operand here (i.e. for codegen selection); the
// ParserMatchClass/DecoderMethod on simm24 itself are unaffected, since
// those only govern hand-written asm and disassembly, not DAG pattern
// matching.
def : Pat<(LTHLWrapper tglobaladdr:$dst), (LD tglobaladdr:$dst)>;
def : Pat<(LTHLWrapper texternalsym:$dst), (LD texternalsym:$dst)>;

// Memory read: loads the word at the address held in $addr into $data.
// opc=0x2 is unused so far (FormR: ADD/AND/OR/XOR/ROR/ROL all use 0x0;
// FormI: LD uses 0x1) -- picking a fresh value keeps this decodable
// unambiguously alongside the existing defs. WIDTH_32 -- see the
// MemWidth comment above FormMR -- keeps this instruction's encoding
// bit-for-bit identical to every READ emitted before the WIDTH field
// existed.
let mayLoad = 1 in
def READ : FormMR<0x2, WIDTH_32.Value, (outs GPR:$data), (ins GPR:$addr),
                  "read $addr, $data",
                  [(set GPR:$data, (load GPR:$addr))]>;

// read8/read16: sub-word loads at native hardware width, replacing the
// old word-per-variable model (a full 32-bit READ plus GPR-side
// mask/sign-extend) for anything that's genuinely byte- or
// halfword-addressed -- packed structs, real char/byte-array storage,
// anything sharing a word with a neighbor. $data still receives a full
// 32-bit GPR value; only the memory-side access width differs. Loaded
// bits land in the low 8/16 bits -- whether the upper bits come back
// zero-extended or sign-extended (or need a Custom-lowered
// zextload/sextload split the way most targets do it) is not yet pinned
// down at the ISel/Pat level; these defs establish the encoding only.
let mayLoad = 1 in {
def READ8 : FormMR<0x2, WIDTH_8.Value, (outs GPR:$data), (ins GPR:$addr),
                    "read8 $addr, $data", []>;
def READ16 : FormMR<0x2, WIDTH_16.Value, (outs GPR:$data), (ins GPR:$addr),
                     "read16 $addr, $data", []>;
}

// Memory write: stores $data to the address held in $addr.
// opc=0x3 is the next unused value (see READ's comment above for the
// ones already taken). WIDTH_32 keeps this bit-for-bit identical to
// every WRITE emitted before the WIDTH field existed.
let mayStore = 1 in

def WRITE : FormMW<0x3, WIDTH_32.Value, (outs), (ins GPR:$addr, GPR:$data),
                    "write $addr, $data",
                    [(store GPR:$data, GPR:$addr)]>;

// write8/write16: sub-word stores at native hardware width -- only the
// low 8/16 bits of $data are written to memory; see READ8/READ16's
// comment above for the same ISel-wiring caveat (encoding only, no
// truncstore Pat yet).
let mayStore = 1 in {
def WRITE8 : FormMW<0x3, WIDTH_8.Value, (outs), (ins GPR:$addr, GPR:$data),
                     "write8 $addr, $data", []>;
def WRITE16 : FormMW<0x3, WIDTH_16.Value, (outs), (ins GPR:$addr, GPR:$data),
                      "write16 $addr, $data", []>;
}
		    
// Condition codes for the conditional jump suffixes on jr/j.
//   <none> = 0x0 - Absolute jump
//   z      = 0x1 - zero flag
//   c      = 0x2 - carry flag
//   v      = 0x3 - overflow flag
//   n      = 0x4 - negative flag
class JMPCondCode<string suffix, bits<5> val> {
  string Suffix = suffix;
  bits<5> Value = val;
}

def COND_ABS : JMPCondCode<"",  0x0>;
def COND_Z   : JMPCondCode<"z", 0x1>;
def COND_C   : JMPCondCode<"c", 0x2>;
def COND_V   : JMPCondCode<"v", 0x3>;
def COND_N   : JMPCondCode<"n", 0x4>;

// jr[<cc>] $target -- register-indirect jump, condition selected by opcode
// suffix. FormJRG bakes `cond` in as a template argument (not an operand),
// so each condition gets its own instantiation. The conditional variants
// read the flags ADD/SUB leave behind (see FLAGS in LTHLRegisterInfo.td);
// the unconditional JR below doesn't.
multiclass JumpReg<JMPCondCode CC> {
  let isBranch = 1, isTerminator = 1, isIndirectBranch = 1, Uses = [FLAGS] in
  def NAME : FormJRG<0x5, CC.Value, (outs), (ins GPR:$target),
                      "jr" # CC.Suffix # " $target",
                      []>;
}

// Absolute (unconditional) register jump is also an unconditional barrier,
// and is the only variant selectable from a generic `brind`.
let isBranch = 1, isTerminator = 1, isBarrier = 1, isIndirectBranch = 1 in
def JR : FormJRG<0x5, COND_ABS.Value, (outs), (ins GPR:$target),
                  "jr $target",
                  [(brind GPR:$target)]>;

defm JRZ : JumpReg<COND_Z>;
defm JRC : JumpReg<COND_C>;
defm JRV : JumpReg<COND_V>;
defm JRN : JumpReg<COND_N>;

// j[<cc>] $rel -- PC-relative jump by signed 16-bit offset, condition
// selected by opcode suffix. FormJRL declares `cond` as a plain bits<5>
// field (not a template arg), so each condition is bound with `let cond`.
// The conditional variants read the flags ADD/SUB leave behind; the
// unconditional J below doesn't.
multiclass JumpRel<JMPCondCode CC> {
  let isBranch = 1, isTerminator = 1, Uses = [FLAGS] in
  def NAME : FormJRL<0x4, (outs), (ins brtarget:$rel),
                      "j" # CC.Suffix # " $rel",
                      []> {
    let cond = CC.Value;
  }
}

// Absolute (unconditional) relative jump is also an unconditional barrier,
// and is the only variant selectable from a generic `br`.
let isBranch = 1, isTerminator = 1, isBarrier = 1 in
def J : FormJRL<0x4, (outs), (ins brtarget:$rel),
                "j $rel",
                [(br bb:$rel)]> {
  let cond = COND_ABS.Value;
}

defm JZ : JumpRel<COND_Z>;
defm JC : JumpRel<COND_C>;
defm JV : JumpRel<COND_V>;
defm JN : JumpRel<COND_N>;

// Real return instruction: same encoding as `jr $target` (FormJRG, cond
// COND_ABS) but with the register field pinned to R26 (the link
// register) instead of taken from an operand, and flagged isReturn=1 so
// PEI/shrink-wrap treat it as a function return rather than a generic
// indirect branch -- that's the one thing JR itself (kept generic for
// brind/switch lowering) can't be used for. Selected from LTHLretglue by
// the pattern below; see LTHLISelLowering::LowerReturn.
//
// isCodeGenOnly=1: its literal AsmString ("jr r26", not "jr $target" --
// there's no operand to fill in, `target` is hard-wired) would otherwise
// collide with JR's "jr $target" pattern in the generated text matcher,
// since r26 is a perfectly ordinary, valid value for JR's own $target
// operand -- "jr r26" typed by hand is genuinely ambiguous between the
// two. Gets its own unambiguous spelling instead, same trick as `mov`
// above: see the InstAlias right below.
let isReturn = 1, isTerminator = 1, isBarrier = 1, isCodeGenOnly = 1,
    Uses = [R26] in
def RET : FormJRG<0x5, COND_ABS.Value, (outs), (ins), "jr r26", []> {
  let target = 26; // R26 -- hard-wired, not a real operand.
}

def : InstAlias<"ret", (RET)>;

// RET_PSEUDO: selected from LTHLretglue in place of RET directly. This
// indirection exists for the same reason CALL_PSEUDO stays a pseudo
// until expandPostRAPseudo (see that def's comment) -- but for the
// opposite end of a non-leaf function's R26 lifetime.
//
// R26 (this function's own incoming return address) is clobbered by
// CALL_PSEUDO's post-RA expansion (Defs=[R26]) whenever this function
// itself makes a call. LTHLFrameLowering::determineCalleeSaves already
// opts R26 into the generic callee-saved spill/restore machinery for
// exactly that case -- but that machinery runs in PEI, which executes
// *after* LiveIntervals/register allocation, not before. If RET's
// static `Uses = [R26]` were attached this early (as it originally was,
// directly on this def, with the Pattern selecting straight onto it),
// LiveIntervals would need a reaching definition of physical R26 all
// the way from function entry to this point on every CFG path --
// including paths that pass through a call that clobbers it -- and
// MachineVerifier correctly flags that as "Using an undefined physical
// register" for any function that both takes R26 live-in (every
// function does, via LowerFormalArguments' addLiveIn) and makes a call
// before returning.
//
// Fix: don't let anything reference physical R26 until *after* PEI has
// already restored it. RET_PSEUDO carries no Uses annotation, so
// LiveIntervals never needs to trace R26 through the function body at
// all. PEI still restores R26 immediately before this pseudo -- its
// `isReturn = 1` is exactly what the generic restoreCalleeSavedRegisters
// insertion point (immediately before a return block's terminator) keys
// off of -- and only *after* that, in expandPostRAPseudo (i.e. after
// PEI, same relative ordering CALL_PSEUDO already relies on), does this
// pseudo get replaced with the real RET above, whose Uses = [R26] is
// now satisfied by an instruction sitting immediately before it, not by
// a value that had to survive the whole function.
//
// Leaf functions (no calls -- determineCalleeSaves skips R26 for them)
// are still correct: R26 is now reserved outright in
// LTHLRegisterInfo::getReservedRegs, so nothing else can be allocated
// into it between entry and this point regardless of whether
// LiveIntervals is tracking it explicitly.
let isReturn = 1, isTerminator = 1, isBarrier = 1 in
def RET_PSEUDO : LTHLPseudo<(outs), (ins), "# RET_PSEUDO", [(LTHLretglue)]>;

// ADDPC: rd = r31 (this instruction's own address -- see arch-base.md;
// confirmed r31 is a completely ordinary, valid R-type operand in
// hardware) + rs2. Bit-for-bit this is just `add $rd, r31, $rs2`
// (FormR, aluop=Adder) -- the only reason it isn't literally the ADD
// def above is that r31 has to be hard-wired into the areg field rather
// than taken from a normal GPR-class operand: LLVM's GPR register class
// deliberately excludes r31 (see LTHLRegisterInfo.td -- the allocator
// must never be free to hand it out as a scratch register), so exposing
// it as a real `ins` operand would fail the operand's register-class
// check. Same trick RET uses to pin R26 into FormJRG's target field.
//
// This exists purely to compute a return address for CALL_PSEUDO's
// expansion -- see LTHLInstrInfo::expandPostRAPseudo.
let Defs = [FLAGS] in
def ADDPC : FormR<0x0, 0x0, (outs GPR:$rd), (ins GPR:$rs2),
                   "add $rd, r31, $rs2", []> {
  let rs1 = 31; // r31 (PC) -- hard-wired, not a real operand.
}

// Call-jump: the unconditional-jump half of a call. Bit-for-bit this is
// identical to `jr $target` (FormJRG, opcode 101, COND_ABS=0x0 -- a real,
// doc-defined condition) -- but it can't just *be* JR, because JR is
// flagged isBranch/isTerminator/isBarrier (nothing after it in the
// block executes), whereas a call returns and falls through to whatever
// comes next. This needs isCall instead, so it gets its own instruction
// purely for that MI-level distinction -- exactly the same reason RET
// is a separate def from JR above despite also sharing FormJRG/COND_ABS.
//
// This only performs the jump. It does NOT write R26 -- see
// LTHLInstrInfo::expandPostRAPseudo for how CALL_PSEUDO expands into
// this plus the return-address math.
//
// isCodeGenOnly=1: its AsmString ("jr $target") is byte-for-byte
// identical to JR's -- the two are genuinely indistinguishable as text,
// since nothing about typed assembly can express "used as a call" (that
// distinction is pure MI-level bookkeeping: isCall vs.
// isBranch/isTerminator/isBarrier, irrelevant to hand-written asm). A
// human wanting this exact instruction just writes `jr $target` and
// gets JR, which is bit-for-bit identical -- CALLJ is only ever reached
// via expandPostRAPseudo's BuildMI, never through text parsing.
let isCall = 1, isCodeGenOnly = 1, Uses = [R26] in
def CALLJ : FormJRG<0x5, COND_ABS.Value, (outs), (ins GPR:$target),
                     "jr $target", []>;

// Pseudo Instructions
// A call clobbers R26 (the return address ends up there) and FLAGS
// (materializing that address needs ADDPC, an ADD-family instruction).
// This stays a plain pseudo all the way through instruction scheduling
// and register allocation -- deliberately NOT expanded early via
// usesCustomInserter, unlike ADDI/BR_CC_PSEUDO below. The return-address
// math only works if the instructions computing it sit at an exact,
// fixed distance from the jump that follows (see ADDPC's comment); an
// early expansion has no way to stop a generic scheduler from
// separating them, silently corrupting the offset. See
// LTHLInstrInfo::expandPostRAPseudo, which runs at the one point in the
// pipeline (after RA, after scheduling) where that's actually
// guaranteed, and bundles the resulting instructions together for good
// measure.
let isCall = 1, isTerminator = 0, isBarrier = 0, Defs = [R26, FLAGS] in
def CALL_PSEUDO : LTHLPseudo<(outs), (ins GPR:$target),
                              "# CALL_PSEUDO $target", []>;

// Selects LTHLTargetLowering::LowerCall's LTHLcall node straight onto
// CALL_PSEUDO -- see that node's comment above for why no C++
// LTHLISelDAGToDAG code is needed for this.
// callseq_start/callseq_end are deliberately NOT predefined globally in
// TargetSelectionDAG.td -- every target defines its own instantiation of
// the SDCallSeqStart/SDCallSeqEnd classes (same as X86/MSP430/etc.),
// since the two immediate operands' exact type constraints are
// target-specific. Needed so ISD::CALLSEQ_START/CALLSEQ_END (emitted
// around every call by SelectionDAGBuilder) have a pattern to match --
// see ADJCALLSTACKDOWN/UP below.
def SDT_LTHLCallSeqStart : SDCallSeqStart<[SDTCisVT<0, i32>, SDTCisVT<1, i32>]>;
def SDT_LTHLCallSeqEnd : SDCallSeqEnd<[SDTCisVT<0, i32>, SDTCisVT<1, i32>]>;
def callseq_start : SDNode<"ISD::CALLSEQ_START", SDT_LTHLCallSeqStart,
                            [SDNPHasChain, SDNPOutGlue]>;
def callseq_end : SDNode<"ISD::CALLSEQ_END", SDT_LTHLCallSeqEnd,
                          [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;

def : Pat<(LTHLcall GPR:$target), (CALL_PSEUDO GPR:$target)>;
def : Pat<(LTHLcall tglobaladdr:$target), (CALL_PSEUDO tglobaladdr:$target)>;
def : Pat<(LTHLcall texternalsym:$target), (CALL_PSEUDO texternalsym:$target)>;
// ADJCALLSTACKDOWN/ADJCALLSTACKUP: the standard pseudo pair every
// SelectionDAG-based target needs so ISD::CALLSEQ_START/CALLSEQ_END
// (emitted around every call, unconditionally, by
// SelectionDAGBuilder -- see LTHLTargetLowering::LowerCall's
// DAG.getCALLSEQ_START/getCALLSEQ_END) have something to select onto.
// LTHLInstrInfo's constructor passes these opcodes through as
// CFSetupOpcode/CFDestroyOpcode to the generated base class -- see
// LTHLInstrInfo.cpp -- which is what lets PrologEpilogInserter
// recognize and erase them.
//
// LTHLFrameLowering doesn't override hasReservedCallFrame() (default:
// true), matching the doc comment on LowerCall's stack-arg handling --
// outgoing stack args are written straight to SP + offset, no actual
// SP adjustment happens around the call -- so PEI's default
// eliminateCallFramePseudoInstr just erases these two outright once
// they've served their only real purpose (letting ISel select the
// CALLSEQ_START/END nodes at all). Same shape as MSP430's
// ADJCALLSTACKDOWN/UP.
let Defs = [R30], Uses = [R30] in {
def ADJCALLSTACKDOWN : LTHLPseudo<(outs), (ins i32imm:$amt1, i32imm:$amt2),
                                   "# ADJCALLSTACKDOWN $amt1, $amt2",
                                   [(callseq_start timm:$amt1, timm:$amt2)]>;
def ADJCALLSTACKUP : LTHLPseudo<(outs), (ins i32imm:$amt1, i32imm:$amt2),
                                 "# ADJCALLSTACKUP $amt1, $amt2",
                                 [(callseq_end timm:$amt1, timm:$amt2)]>;
}


// Materializes the address of a stack slot into a GPR. LTHL has no
// addressing mode that can take a frame index directly (see the
// FormMR/FormMW comment on READ/WRITE), so a bare ISD::FrameIndex
// reaching the DAG -- the base pointer for a stack-passed argument's
// load in LTHLTargetLowering::LowerFormalArguments, or the address of
// an address-taken/aggregate local -- has to become an ordinary i32
// value in a GPR before anything else can use it. LTHLISelDAGToDAG's
// Select() builds this directly (CurDAG->SelectNodeTo), the same way
// storeRegToStackSlot/loadRegFromStackSlot hand a raw FrameIndex
// operand straight to WRITE/READ for spills -- no Pat is needed or
// possible here, since nothing produces a bare frameindex node from a
// declarative pattern. $fi carries the FrameIndex operand itself (not
// a real register) until LTHLRegisterInfo::eliminateFrameIndex resolves
// it in place during PEI, exactly like WRITE/READ's $addr; only once
// that's happened does LTHLInstrInfo::expandPostRAPseudo turn this into
// a real `add $dst, $fi, r0` (the same "mov" idiom copyPhysReg uses).
def ADDRFI : LTHLPseudo<(outs GPR:$dst), (ins GPR:$fi),
                         "# ADDRFI $dst, $fi", []>;

// Add-immediate: LTHL has no single instruction that adds a register and
// an immediate in one shot, so this expands (via custom inserter, since
// it needs a fresh scratch vreg) into:
//   LD  <scratch>, $imm
//   ADD $rd, $rs1, <scratch>
// simm24 matches LD's own immediate width, since that's the instruction
// doing the actual immediate load.
let usesCustomInserter = 1 in
def ADDI : LTHLPseudo<(outs GPR:$rd), (ins GPR:$rs1, simm24:$imm),
                       "# ADDI $rd, $rs1, $imm",
                       [(set GPR:$rd, (add GPR:$rs1, imm:$imm))]>;

// Compare-and-branch: LTHL's jr*/j* instructions only test "is this flag
// SET" -- there's no complementary "jnz"-style instruction -- so turning
// an arbitrary ISD::CondCode comparison into a branch needs more than a
// single Pat can express (SETNE, for instance, needs an extra
// unconditional jump to invert the sense; see the isBranch comment
// below). LTHLTargetLowering::LowerBR_CC builds this node directly from
// ISD::BR_CC (marked Custom in the constructor), and
// LTHLTargetLowering::emitBR_CC (the custom inserter) does the real
// work: a SUB for its FLAGS side effect, then the jr*/j* sequence for
// whichever CondCode was requested. Only SETEQ/SETNE are implemented
// today -- see the report_fatal_error in LowerBR_CC for why the rest
// (ordered comparisons, which would need combining multiple flags)
// aren't.
def SDT_LTHLBrcc
    : SDTypeProfile<0, 4, [SDTCisVT<0, i32>, SDTCisSameAs<0, 1>,
                            SDTCisVT<2, i32>, SDTCisVT<3, OtherVT>]>;
def LTHLbrcc : SDNode<"LTHLISD::BR_CC", SDT_LTHLBrcc, [SDNPHasChain]>;

let isBranch = 1, isTerminator = 1, usesCustomInserter = 1 in
def BR_CC_PSEUDO : LTHLPseudo<(outs),
                               (ins GPR:$lhs, GPR:$rhs, i32imm:$cc,
                                brtarget:$dest),
                               "# BR_CC_PSEUDO $lhs, $rhs, $cc, $dest",
                               [(LTHLbrcc GPR:$lhs, GPR:$rhs, timm:$cc,
                                 bb:$dest)]>;

// Standalone SETCC: materializes a boolean (0/1) comparison result into
// a GPR, rather than branching on it -- the case BR_CC_PSEUDO doesn't
// cover. This shows up whenever a frontend (or an unoptimized -O0-style
// IR pattern) computes a comparison as an ordinary i32/i1 value instead
// of folding it straight into a branch -- e.g. `%cmp = icmp eq i32 %x,
// 0` used by something other than a `br`. LTHL has no conditional-move
// or flags-to-register instruction (see the "SELECT/SELECT_CC/
// standalone SETCC" comment in LTHLISelLowering.cpp's constructor), so
// this needs the same branch-based diamond CFG expansion BR_CC_PSEUDO's
// custom inserter already knows how to build -- see
// LTHLTargetLowering::emitSETCC, which reuses emitCondBranch (the exact
// same CondCode -> jr*/j* sequence emitBR_CC uses) aimed at a true/false
// block pair instead of a branch target/fallthrough, then LDs 1 or 0
// into a fresh vreg in each and PHIs them back together.
def SDT_LTHLSetcc
    : SDTypeProfile<1, 3, [SDTCisVT<0, i32>, SDTCisSameAs<1, 2>,
                            SDTCisVT<1, i32>, SDTCisVT<3, i32>]>;
def LTHLsetcc : SDNode<"LTHLISD::SETCC", SDT_LTHLSetcc, []>;

let usesCustomInserter = 1 in
def SETCC_PSEUDO : LTHLPseudo<(outs GPR:$rd),
                               (ins GPR:$lhs, GPR:$rhs, i32imm:$cc),
                               "# SETCC_PSEUDO $rd, $lhs, $rhs, $cc",
                               [(set GPR:$rd, (LTHLsetcc GPR:$lhs, GPR:$rhs,
                                 timm:$cc))]>;

// SELECT_CC: materializes one of two arbitrary GPR values depending on
// a comparison, rather than a hardcoded 0/1 (that's SETCC_PSEUDO just
// above) or a branch (BR_CC_PSEUDO). LTHL has no conditional-move
// instruction (see the LowerOperation constructor comment in
// LTHLISelLowering.cpp), so ISD::SELECT_CC has to become the same
// branch-based diamond CFG SETCC_PSEUDO already builds -- see
// LTHLTargetLowering::emitSELECT_CC, which reuses emitCondBranch (the
// exact same CondCode -> jr*/j* sequence emitBR_CC/emitSETCC use) aimed
// at a true/false block pair, then PHIs $tval/$fval back together
// instead of two freshly-LD'd constants. Plain ISD::SELECT is marked
// Expand in the constructor, which routes it through this same node
// (SelectionDAGLegalize's default ExpandSELECT rewrites SELECT as
// SELECT_CC against zero with SETNE) -- so this one pseudo covers both
// source-level selects and ternaries.
def SDT_LTHLSelectCC
    : SDTypeProfile<1, 5, [SDTCisVT<0, i32>, SDTCisVT<1, i32>,
                            SDTCisSameAs<1, 2>, SDTCisSameAs<0, 3>,
                            SDTCisSameAs<0, 4>, SDTCisVT<5, i32>]>;
def LTHLselectcc : SDNode<"LTHLISD::SELECT_CC", SDT_LTHLSelectCC, []>;

let usesCustomInserter = 1 in
def SELECT_CC_PSEUDO : LTHLPseudo<(outs GPR:$rd),
                                   (ins GPR:$lhs, GPR:$rhs, GPR:$tval,
                                    GPR:$fval, i32imm:$cc),
                                   "# SELECT_CC_PSEUDO $rd, $lhs, $rhs, $tval, $fval, $cc",
                                   [(set GPR:$rd, (LTHLselectcc GPR:$lhs,
                                     GPR:$rhs, GPR:$tval, GPR:$fval,
                                     timm:$cc))]>;

// Variable-amount shifts. LTHL's ALU has no shift instruction at all --
// only 1-bit rotate-through-carry (ROR/ROL), which themselves have no
// ISel Pattern (see their comment above -- generic rotl/rotr ISD nodes
// don't carry the FLAGS dependency they need). A shift by an arbitrary
// runtime amount is built entirely out of already-legal ALU ops via a
// generated loop -- see LTHLTargetLowering::emitShift:
//   SHL by 1 = ADD rd, rs, rs         (self-add; mod-2^32 overflow is
//                                       exactly a left shift by one)
//   SRL by 1 = clear Carry (ADD r0,r0,r0), then ROR rd, rs, r0 -- the
//                                       cleared carry fills the vacated
//                                       top bit with 0
//   SRA by 1 = ADD scratch,rs,rs (result discarded -- only its Carry
//                                 out, which equals rs's original MSB,
//                                 matters), then ROR rd, rs, r0 -- the
//                                 saved sign bit rotates back in as the
//                                 new top bit
// looped `amt` times with a decrementing counter, same usesCustomInserter
// idiom as ADDI/BR_CC_PSEUDO above. Correct but O(amt) at runtime --
// worst case 31 iterations -- since there's no barrel shifter; constant
// shift amounts aren't special-cased/unrolled yet, a possible future
// optimization.
let usesCustomInserter = 1 in {
def SHL_PSEUDO : LTHLPseudo<(outs GPR:$rd), (ins GPR:$rs1, GPR:$amt),
                             "# SHL_PSEUDO $rd, $rs1, $amt",
                             [(set GPR:$rd, (shl GPR:$rs1, GPR:$amt))]>;
def SRL_PSEUDO : LTHLPseudo<(outs GPR:$rd), (ins GPR:$rs1, GPR:$amt),
                             "# SRL_PSEUDO $rd, $rs1, $amt",
                             [(set GPR:$rd, (srl GPR:$rs1, GPR:$amt))]>;
def SRA_PSEUDO : LTHLPseudo<(outs GPR:$rd), (ins GPR:$rs1, GPR:$amt),
                             "# SRA_PSEUDO $rd, $rs1, $amt",
                             [(set GPR:$rd, (sra GPR:$rs1, GPR:$amt))]>;
}

// Map the sub-word load/store nodes onto the new native-width
// instructions -- see the lthl_load8/lthl_load16/lthl_store8/
// lthl_store16 defs above.
def : Pat<(lthl_load8 GPR:$addr), (READ8 GPR:$addr)>;
def : Pat<(lthl_load16 GPR:$addr), (READ16 GPR:$addr)>;
def : Pat<(lthl_store8 GPR:$data, GPR:$addr), (WRITE8 GPR:$addr, GPR:$data)>;
def : Pat<(lthl_store16 GPR:$data, GPR:$addr), (WRITE16 GPR:$addr, GPR:$data)>;
