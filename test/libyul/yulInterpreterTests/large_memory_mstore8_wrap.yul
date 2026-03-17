{
    // mstore8 at max u256 address: single byte write, no range wrapping
    mstore8(sub(0, 1), 0xab)
    // mload at same address: reads 32 bytes wrapping around zero
    // byte 0: memory[0xfff...fff] = 0xab, bytes 1-31: memory[0..30] = 0
    sstore(0, mload(sub(0, 1)))
}
// ----
// Trace:
// Memory dump:
//   FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFE0: 00000000000000000000000000000000000000000000000000000000000000ab
// Storage dump:
//   0000000000000000000000000000000000000000000000000000000000000000: ab00000000000000000000000000000000000000000000000000000000000000
// Transient storage dump:
