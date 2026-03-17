{
    // Calling setimmutable twice with the same key: last write wins.
    setimmutable(0, "slot", 0xaaaa)
    setimmutable(0, "slot", 0xbbbb)
    let v := loadimmutable("slot")
    sstore(0, v)
}
// ====
// bytecodeFormat: legacy
// ----
// Trace:
// Memory dump:
// Storage dump:
//   0000000000000000000000000000000000000000000000000000000000000000: 000000000000000000000000000000000000000000000000000000000000bbbb
// Transient storage dump:
