{
    // Topics larger than 0x1000000 are formatted as hex in the trace.
    // Verify that both the topics and the data are rendered correctly.
    mstore(0, 0x5a5a)
    log2(0, 32, 0xdeadbeef, 0xcafecafe)
}
// ----
// Trace:
//   LOG2(0, 32, 0xdeadbeef, 0xcafecafe) [0000000000000000000000000000000000000000000000000000000000005a5a]
// Memory dump:
//      0: 0000000000000000000000000000000000000000000000000000000000005a5a
// Storage dump:
// Transient storage dump:
