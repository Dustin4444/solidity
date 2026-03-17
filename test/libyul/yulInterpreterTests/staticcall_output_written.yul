{
    // A successful staticcall writes deterministic output bytes to the output region.
    // The interpreter seeds each output byte from the callee address:
    // output byte[i] = (addr >> (8 * (i % 8))) & 0xff, cycling every 8 bytes.
    // staticcall(gas, addr=0x03, inOffset=0, inSize=0, outOffset=32, outSize=8)
    // Address 0x03 is odd and not self → succeeds (returns 1).
    // 8 output bytes at offset 32: 03 00 00 00 00 00 00 00.
    // mload(0x20) reads 32 bytes; first byte is 0x03, rest are zero → 0x0300...00.
    let ok := staticcall(gas(), 0x03, 0, 0, 0x20, 8)
    sstore(0, ok)
    sstore(1, mload(0x20))
}
// ====
// EVMVersion: >=byzantium
// bytecodeFormat: legacy
// ----
// Trace:
//   STATICCALL(153, 3, 0, 0, 32, 8)
// Memory dump:
//     20: 0300000000000000000000000000000000000000000000000000000000000000
// Storage dump:
//   0000000000000000000000000000000000000000000000000000000000000000: 0000000000000000000000000000000000000000000000000000000000000001
//   0000000000000000000000000000000000000000000000000000000000000001: 0300000000000000000000000000000000000000000000000000000000000000
// Transient storage dump:
