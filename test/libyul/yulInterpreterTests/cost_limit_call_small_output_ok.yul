{
    // A successful call with outSize=32 charges only 1 word-cost unit.
    // With maxCost: 100, this completes without hitting the cost limit.
    // call(gas, addr=0x01, value=0, inOffset=0, inSize=0, outOffset=0, outSize=32)
    // Address 0x01 is odd → succeeds. Output byte pattern 01 00 00 00 ... repeats.
    let ok := call(gas(), 0x01, 0, 0, 0, 0, 32)
    sstore(0, ok)
    sstore(1, mload(0))
}
// ====
// bytecodeFormat: legacy
// maxCost: 100
// ----
// Trace:
//   CALL(153, 1, 0, 0, 0, 0, 32)
// Memory dump:
//      0: 0100000000000000010000000000000001000000000000000100000000000000
// Storage dump:
//   0000000000000000000000000000000000000000000000000000000000000000: 0000000000000000000000000000000000000000000000000000000000000001
//   0000000000000000000000000000000000000000000000000000000000000001: 0100000000000000010000000000000001000000000000000100000000000000
// Transient storage dump:
