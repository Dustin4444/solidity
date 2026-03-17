{
    // Write individual bytes via mstore8, then log the region.
    // Each byte position should appear correctly in the log data.
    mstore8(0, 0xaa)
    mstore8(1, 0xbb)
    mstore8(2, 0xcc)
    mstore8(3, 0xdd)
    log0(0, 4)
}
// ----
// Trace:
//   LOG0(0, 4) [aabbccdd]
// Memory dump:
//      0: aabbccdd00000000000000000000000000000000000000000000000000000000
// Storage dump:
// Transient storage dump:
