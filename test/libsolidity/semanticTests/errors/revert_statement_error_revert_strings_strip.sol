contract C {
    error MyError(string message);
    function revertStatement() pure external {
        revert MyError("Error");
    }
}
// ====
// revertStrings: strip
// ----
// revertStatement() -> FAILURE, hex"8b3d2d43", hex"0000000000000000000000000000000000000000000000000000000000000020", hex"0000000000000000000000000000000000000000000000000000000000000005", hex"4572726f72000000000000000000000000000000000000000000000000000000"
