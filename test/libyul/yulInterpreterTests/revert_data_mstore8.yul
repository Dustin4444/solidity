{
    // REVERT data written byte-by-byte via mstore8.
    mstore8(0, 0xca)
    mstore8(1, 0xfe)
    mstore8(2, 0xd0)
    mstore8(3, 0x0d)
    revert(0, 4)
}
// ----
// Trace:
//   REVERT(0, 4) [cafed00d]
// Memory dump:
//      0: cafed00d00000000000000000000000000000000000000000000000000000000
// Storage dump:
// Transient storage dump:
