{
    // Write a known value to memory, then log it with LOG0 and LOG1.
    // The data bytes from memory must appear in the trace entry.
    mstore(0, 0x42)
    log0(0, 32)
    log1(0, 32, 0xaabbccdd)
}
// ----
// Trace:
//   LOG0(0, 32) [0000000000000000000000000000000000000000000000000000000000000042]
//   LOG1(0, 32, 0xaabbccdd) [0000000000000000000000000000000000000000000000000000000000000042]
// Memory dump:
//      0: 0000000000000000000000000000000000000000000000000000000000000042
// Storage dump:
// Transient storage dump:
