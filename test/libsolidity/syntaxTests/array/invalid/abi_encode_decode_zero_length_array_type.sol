contract C {
    function f() public returns (bytes memory) {
        return abi.encode(abi.decode("", (int[0])));
    }
}
// ----
// TypeError 1406: (108-109): Array with zero length specified.
