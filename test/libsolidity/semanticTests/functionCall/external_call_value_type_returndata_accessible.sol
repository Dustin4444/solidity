interface I {
    function getUint() external returns (uint256);
}

contract Impl is I {
    function getUint() external pure returns (uint256) { return 42; }
}

// Verify returndatasize/returndatacopy still work after value-type call
// even though we skip memory allocation for the return value.
contract C {
    I immutable impl = I(address(new Impl()));

    function testReturndataAccessible() public returns (uint size, uint rawValue) {
        impl.getUint();
        assembly {
            size := returndatasize()
            returndatacopy(0, 0, 32)
            rawValue := mload(0)
        }
    }
}
// ====
// EVMVersion: >=byzantium
// compileViaYul: true
// ----
// testReturndataAccessible() -> 32, 42
