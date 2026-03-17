{
    // Simple arithmetic with a generous maxCost should complete normally
    // and not trigger InstructionLimitReached.
    mstore(0, add(mul(3, 7), 1))
}
// ====
// maxCost: 10000
// ----
// Trace:
// Memory dump:
//      0: 0000000000000000000000000000000000000000000000000000000000000016
// Storage dump:
// Transient storage dump:
