{
    // A failing call must NOT overwrite the output memory region.
    // The interpreter treats even addresses (that are not self) as failures.
    // call(gas, addr=0x02, value=0, inOffset=0, inSize=0, outOffset=0, outSize=8)
    // Address 0x02 is even and not self → fails (returns 0).
    // Memory is pre-filled with 0xff...ff; after the failed call it must be unchanged.
    // Storage slot 0 holds 0 (call failed), slot 1 holds the untouched 0xff...ff word.
    mstore(0, 0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff)
    let ok := call(gas(), 0x02, 0, 0, 0, 0, 8)
    sstore(0, ok)
    sstore(1, mload(0))
}
// ====
// bytecodeFormat: legacy
// ----
// Trace:
//   CALL(153, 2, 0, 0, 0, 0, 8)
// Memory dump:
//      0: ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff
// Storage dump:
//   0000000000000000000000000000000000000000000000000000000000000001: ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff
// Transient storage dump:
