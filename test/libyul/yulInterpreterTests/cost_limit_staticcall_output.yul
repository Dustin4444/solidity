{
    // A successful staticcall with outSize=0x200 (512 bytes) charges 16 word-cost units.
    // With maxCost: 5, the output memory write triggers InstructionLimitReached.
    // staticcall(gas, addr=0x03, inOffset=0, inSize=0, outOffset=0, outSize=512)
    // Address 0x03 is odd → succeeds, then the cost budget is exceeded.
    let ok := staticcall(gas(), 0x03, 0, 0, 0, 0x200)
    sstore(0, ok)
}
// ====
// EVMVersion: >=byzantium
// bytecodeFormat: legacy
// maxCost: 5
// ----
// Trace:
//   STATICCALL(153, 3, 0, 0, 0, 512)
//   Instruction cost limit reached.
// Memory dump:
// Storage dump:
// Transient storage dump:
