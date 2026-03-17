{
    // A successful call to an odd-addressed contract writes deterministic bytes
    // to the output memory region.  The interpreter uses the callee address as a
    // seed: output byte[i] = (addr >> (8 * (i % 8))) & 0xff, cycling every 8 bytes.
    // call(gas, addr=0x01, value=0, inOffset=0, inSize=0, outOffset=0, outSize=8)
    // Address 0x01 is odd and not self → succeeds (returns 1).
    // 8 output bytes starting at offset 0: 01 00 00 00 00 00 00 00.
    // mload(0) reads 32 bytes; the first byte is 0x01 and the rest are zero,
    // so storage slot 1 holds 0x0100...00.
    let ok := call(gas(), 0x01, 0, 0, 0, 0, 8)
    sstore(0, ok)
    sstore(1, mload(0))
}
// ====
// bytecodeFormat: legacy
// ----
// Trace:
//   CALL(153, 1, 0, 0, 0, 0, 8)
// Memory dump:
//      0: 0100000000000000000000000000000000000000000000000000000000000000
// Storage dump:
//   0000000000000000000000000000000000000000000000000000000000000000: 0000000000000000000000000000000000000000000000000000000000000001
//   0000000000000000000000000000000000000000000000000000000000000001: 0100000000000000000000000000000000000000000000000000000000000000
// Transient storage dump:
