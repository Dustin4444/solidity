{
    // Store a value in storage and memory, then revert.
    // The revert trace entry must contain the memory data.
    // Storage must be cleared (revert semantics).
    sstore(0, 1)
    mstore(0, 0xdeadbeef)
    revert(28, 4)
}
// ----
// Trace:
//   REVERT(28, 4) [deadbeef]
// Memory dump:
//      0: 00000000000000000000000000000000000000000000000000000000deadbeef
// Storage dump:
// Transient storage dump:
