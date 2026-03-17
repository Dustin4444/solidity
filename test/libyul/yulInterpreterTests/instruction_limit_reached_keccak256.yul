{
    // keccak256 charges 50 cost units per call (plus 1 for evalBuiltin base).
    // Two calls = ~102 cost. With maxCost: 60, the second call triggers InstructionLimitReached.
    let a := keccak256(0, 0)
    let b := keccak256(0, 0)
    mstore(0, xor(a, b))
}
// ====
// maxCost: 60
// ----
// Trace:
//   Instruction cost limit reached.
// Memory dump:
// Storage dump:
// Transient storage dump:
