{
	// delegatecall(gas, addr=0x45, inOffset=0, inSize=32, outOffset=48, outSize=32)
	// Address 0x45 is odd and not self → succeeds (returns 1).
	// The interpreter writes deterministic output bytes derived from the callee address
	// to the output region [48, 80): address byte pattern 45 00 00 00 00 00 00 00
	// repeats every 8 bytes, producing two non-zero 32-byte chunks in the memory dump.
	let x := delegatecall(gas(), 0x45, 0, 0x20, 0x30, 0x20)
	sstore(100, x)
}
// ====
// bytecodeFormat: legacy
// ----
// Trace:
//   DELEGATECALL(153, 69, 0, 32, 48, 32)
// Memory dump:
//     20: 0000000000000000000000000000000045000000000000004500000000000000
//     40: 4500000000000000450000000000000000000000000000000000000000000000
// Storage dump:
//   0000000000000000000000000000000000000000000000000000000000000064: 0000000000000000000000000000000000000000000000000000000000000001
// Transient storage dump:
