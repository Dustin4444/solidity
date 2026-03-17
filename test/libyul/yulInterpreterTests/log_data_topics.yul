{
    // Write a known value to memory, then emit LOG2/LOG3/LOG4.
    // All three log instructions must include the memory data in their trace entries.
    mstore(0, 0x1234)
    log2(0, 32, 1, 2)
    log3(0, 32, 3, 4, 5)
    log4(0, 32, 6, 7, 8, 9)
}
// ----
// Trace:
//   LOG2(0, 32, 1, 2) [0000000000000000000000000000000000000000000000000000000000001234]
//   LOG3(0, 32, 3, 4, 5) [0000000000000000000000000000000000000000000000000000000000001234]
//   LOG4(0, 32, 6, 7, 8, 9) [0000000000000000000000000000000000000000000000000000000000001234]
// Memory dump:
//      0: 0000000000000000000000000000000000000000000000000000000000001234
// Storage dump:
// Transient storage dump:
