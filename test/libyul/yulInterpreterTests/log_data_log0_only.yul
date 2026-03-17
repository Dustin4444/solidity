{
    // LOG0 has no topics. Only the data field distinguishes it.
    // Use a distinctive bit pattern to verify correct data capture.
    mstore(0, 0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff)
    log0(0, 32)
}
// ----
// Trace:
//   LOG0(0, 32) [ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff]
// Memory dump:
//      0: ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff
// Storage dump:
// Transient storage dump:
