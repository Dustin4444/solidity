{
    // A successful delegatecall with outSize=0x200 (512 bytes) charges 16 word-cost units.
    // With maxCost: 5, the output memory write triggers InstructionLimitReached.
    // delegatecall(gas, addr=0x45, inOffset=0, inSize=0, outOffset=0, outSize=512)
    // Address 0x45 is odd → succeeds, then the cost budget is exceeded.
    let ok := delegatecall(gas(), 0x45, 0, 0, 0, 0x200)
    sstore(0, ok)
}
// ====
// bytecodeFormat: legacy
// maxCost: 5
// ----
// Trace:
//   DELEGATECALL(153, 69, 0, 0, 0, 512)
//   Instruction cost limit reached.
// Memory dump:
// Storage dump:
// Transient storage dump:
