{
    // Overwriting a large slot: only the last written value persists
    sstore(sub(0, 1), 0x1111)
    sstore(sub(0, 1), 0x2222)
    sstore(sub(0, 1), 0x3333)
    sstore(0, sload(sub(0, 1)))
}
// ----
// Trace:
// Memory dump:
// Storage dump:
//   0000000000000000000000000000000000000000000000000000000000000000: 0000000000000000000000000000000000000000000000000000000000003333
//   ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff: 0000000000000000000000000000000000000000000000000000000000003333
// Transient storage dump:
