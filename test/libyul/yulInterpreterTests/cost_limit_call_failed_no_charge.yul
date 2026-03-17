{
    // A failed call does NOT charge output copy cost because no bytes are written.
    // call(gas, addr=0x02, value=0, inOffset=0, inSize=0, outOffset=0, outSize=0x200)
    // Address 0x02 is even and not self → fails. No output copy occurs, no cost charged.
    // With maxCost: 5, this completes without hitting the cost limit.
    let ok := call(gas(), 0x02, 0, 0, 0, 0, 0x200)
    sstore(0, ok)
}
// ====
// bytecodeFormat: legacy
// maxCost: 5
// ----
// Trace:
//   CALL(153, 2, 0, 0, 0, 0, 512)
// Memory dump:
// Storage dump:
// Transient storage dump:
