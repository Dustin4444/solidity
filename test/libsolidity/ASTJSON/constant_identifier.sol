uint constant X = 1234;
contract C {
    uint constant Y = 5678;
    function f() public returns (uint) {
        return X + Y;
    }
}

// ----
