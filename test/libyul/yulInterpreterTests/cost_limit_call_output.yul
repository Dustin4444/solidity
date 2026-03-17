{
    // A successful call with outSize=0x200 (512 bytes) charges chargeCopyWordCost(512) = 16 word units.
    // With maxCost: 5, the call output write triggers InstructionLimitReached.
    // call(gas, addr=0x01, value=0, inOffset=0, inSize=0, outOffset=0, outSize=512)
    // Address 0x01 is odd → succeeds, then the output copy exceeds the cost budget.
    let ok := call(gas(), 0x01, 0, 0, 0, 0, 0x200)
    sstore(0, ok)
}
// ====
// bytecodeFormat: legacy
// maxCost: 5
// ----
// Trace:
//   CALL(153, 1, 0, 0, 0, 0, 512)
//   Instruction cost limit reached.
// Memory dump:
// Storage dump:
// Transient storage dump:
