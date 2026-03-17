{
    // sstore/sload roundtrip at the maximum storage slot (sub(0,1) = 0xfff...fff)
    sstore(sub(0, 1), 0xdeadbeef)
    sstore(0, sload(sub(0, 1)))
}
// ----
// Trace:
// Memory dump:
// Storage dump:
//   0000000000000000000000000000000000000000000000000000000000000000: 00000000000000000000000000000000000000000000000000000000deadbeef
//   ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff: 00000000000000000000000000000000000000000000000000000000deadbeef
// Transient storage dump:
