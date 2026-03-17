{
    // REVERT must clear both persistent storage and transient storage.
    sstore(0, 0x11)
    sstore(1, 0x22)
    tstore(0, 0x33)
    mstore(0, 0xff)
    revert(31, 1)
}
// ====
// EVMVersion: >=cancun
// ----
// Trace:
//   REVERT(31, 1) [ff]
// Memory dump:
//      0: 00000000000000000000000000000000000000000000000000000000000000ff
// Storage dump:
// Transient storage dump:
