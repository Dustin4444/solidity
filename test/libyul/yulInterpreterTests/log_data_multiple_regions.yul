{
    // Two LOGs reading from different memory regions.
    // Each must carry the data from its own region independently.
    mstore(0x00, 0xaaaa)
    mstore(0x20, 0xbbbb)
    log0(0x00, 32)
    log0(0x20, 32)
}
// ----
// Trace:
//   LOG0(0, 32) [000000000000000000000000000000000000000000000000000000000000aaaa]
//   LOG0(32, 32) [000000000000000000000000000000000000000000000000000000000000bbbb]
// Memory dump:
//      0: 000000000000000000000000000000000000000000000000000000000000aaaa
//     20: 000000000000000000000000000000000000000000000000000000000000bbbb
// Storage dump:
// Transient storage dump:
