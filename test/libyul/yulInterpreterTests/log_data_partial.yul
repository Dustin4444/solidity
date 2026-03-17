{
    // Log only a partial slice of a word. The data field should contain
    // exactly the bytes requested, not the full 32-byte word.
    mstore(0, 0xdeadbeefcafebabe)
    log0(24, 8)
}
// ----
// Trace:
//   LOG0(24, 8) [deadbeefcafebabe]
// Memory dump:
//      0: 000000000000000000000000000000000000000000000000deadbeefcafebabe
// Storage dump:
// Transient storage dump:
