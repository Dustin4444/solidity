{
    // loadimmutable for an identifier never passed to setimmutable returns
    // a deterministic keccak-based fallback value rather than zero.
    let v := loadimmutable("unset_var")
    // Store non-zero means fallback value was returned.
    sstore(0, iszero(iszero(v)))
}
// ====
// bytecodeFormat: legacy
// ----
// Trace:
// Memory dump:
// Storage dump:
//   0000000000000000000000000000000000000000000000000000000000000000: 0000000000000000000000000000000000000000000000000000000000000001
// Transient storage dump:
