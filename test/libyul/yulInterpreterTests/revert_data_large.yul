{
    // REVERT with a full 32-byte data payload.
    // Verifies the full word is captured in the trace.
    mstore(0, 0xdeadbeefcafebabe1234567890abcdef)
    revert(0, 32)
}
// ----
// Trace:
//   REVERT(0, 32) [00000000000000000000000000000000deadbeefcafebabe1234567890abcdef]
// Memory dump:
//      0: 00000000000000000000000000000000deadbeefcafebabe1234567890abcdef
// Storage dump:
// Transient storage dump:
