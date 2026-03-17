{
    // REVERT after overwriting memory. The trace must carry the final value.
    mstore(0, 0xaaaa)
    mstore(0, 0xbbbb)
    revert(0, 32)
}
// ----
// Trace:
//   REVERT(0, 32) [000000000000000000000000000000000000000000000000000000000000bbbb]
// Memory dump:
//      0: 000000000000000000000000000000000000000000000000000000000000bbbb
// Storage dump:
// Transient storage dump:
