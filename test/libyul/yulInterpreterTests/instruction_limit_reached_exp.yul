{
    // exp(2, not(0)) charges 1 + 256 = 257 cost units (1 + bits in exponent).
    // With maxCost: 100, this must trigger InstructionLimitReached.
    let x := exp(2, not(0))
    mstore(0, x)
}
// ====
// maxCost: 100
// ----
// Trace:
//   Instruction cost limit reached.
// Memory dump:
// Storage dump:
// Transient storage dump:
