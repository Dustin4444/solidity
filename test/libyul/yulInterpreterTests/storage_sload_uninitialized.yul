{
    // sload of a slot that was never written returns 0
    sstore(0, sload(0xdeadbeef))
    sstore(1, sload(sub(0, 1)))
}
// ----
// Trace:
// Memory dump:
// Storage dump:
// Transient storage dump:
