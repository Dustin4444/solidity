{
    // mstore at sub(0, 32) = 0xfff...ffe0: writes exactly 32 bytes ending at 0xfff...ffff,
    // no address wrapping needed
    mstore(sub(0, 32), 0xdeadbeef)
    sstore(0, mload(sub(0, 32)))
}
// ----
// Trace:
// Memory dump:
//   FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFE0: 00000000000000000000000000000000000000000000000000000000deadbeef
// Storage dump:
//   0000000000000000000000000000000000000000000000000000000000000000: 00000000000000000000000000000000000000000000000000000000deadbeef
// Transient storage dump:
