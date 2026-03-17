{
    // Write a value to memory, then overwrite it. The log must reflect
    // the final value, not the original write.
    mstore(0, 0x1111)
    mstore(0, 0x2222)
    log0(0, 32)
}
// ----
// Trace:
//   LOG0(0, 32) [0000000000000000000000000000000000000000000000000000000000002222]
// Memory dump:
//      0: 0000000000000000000000000000000000000000000000000000000000002222
// Storage dump:
// Transient storage dump:
