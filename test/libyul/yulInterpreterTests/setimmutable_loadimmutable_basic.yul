{
    // setimmutable stores a value that loadimmutable can read back.
    // Both builtins use argument 1 as the string identifier.
    setimmutable(0, "myvar", 0xdeadbeef)
    let v := loadimmutable("myvar")
    sstore(0, v)
}
// ====
// bytecodeFormat: legacy
// ----
// Trace:
// Memory dump:
// Storage dump:
//   0000000000000000000000000000000000000000000000000000000000000000: 00000000000000000000000000000000000000000000000000000000deadbeef
// Transient storage dump:
