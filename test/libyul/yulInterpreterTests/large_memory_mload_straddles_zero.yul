{
    // Write distinct bytes on either side of the u256 wraparound boundary
    mstore8(sub(0, 1), 0xaa)   // byte at 0xfff...fff
    mstore8(0, 0xbb)            // byte at 0
    // mload at sub(0,1) reads 32 bytes wrapping around zero:
    // byte 0: memory[0xfff...fff] = 0xaa
    // byte 1: memory[0] = 0xbb
    // bytes 2-31: 0
    sstore(0, mload(sub(0, 1)))
}
// ----
// Trace:
// Memory dump:
//      0: bb00000000000000000000000000000000000000000000000000000000000000
//   FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFE0: 00000000000000000000000000000000000000000000000000000000000000aa
// Storage dump:
//   0000000000000000000000000000000000000000000000000000000000000000: aabb000000000000000000000000000000000000000000000000000000000000
// Transient storage dump:
