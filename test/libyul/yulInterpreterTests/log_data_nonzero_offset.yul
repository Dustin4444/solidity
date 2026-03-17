{
    // Log data from a non-zero memory offset.
    // Ensures the offset is applied correctly when reading log data.
    mstore(0x40, 0xfeedface)
    log1(0x40, 32, 0x1)
}
// ----
// Trace:
//   LOG1(64, 32, 1) [00000000000000000000000000000000000000000000000000000000feedface]
// Memory dump:
//     40: 00000000000000000000000000000000000000000000000000000000feedface
// Storage dump:
// Transient storage dump:
