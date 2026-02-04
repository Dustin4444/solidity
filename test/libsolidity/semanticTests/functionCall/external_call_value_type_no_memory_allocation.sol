interface I {
    function getBool() external returns (bool);
    function getUint() external returns (uint256);
    function getAddress() external returns (address);
    function getBytes32() external returns (bytes32);
}

contract Impl is I {
    function getBool() external pure returns (bool) { return true; }
    function getUint() external pure returns (uint256) { return 42; }
    function getAddress() external pure returns (address) { return address(1); }
    function getBytes32() external pure returns (bytes32) { return bytes32(uint256(0xabcd)); }
}

// Value types are decoded directly to the stack and don't need memory.
contract C {
    I immutable impl = I(address(new Impl()));

    function freeMemory() internal pure returns (uint m) { assembly { m := mload(0x40) } }

    function testBool() public returns (uint memDiff) {
        uint memBefore = freeMemory();
        impl.getBool();
        memDiff = freeMemory() - memBefore;
    }

    function testUint() public returns (uint memDiff) {
        uint memBefore = freeMemory();
        impl.getUint();
        memDiff = freeMemory() - memBefore;
    }

    function testAddress() public returns (uint memDiff) {
        uint memBefore = freeMemory();
        impl.getAddress();
        memDiff = freeMemory() - memBefore;
    }

    function testBytes32() public returns (uint memDiff) {
        uint memBefore = freeMemory();
        impl.getBytes32();
        memDiff = freeMemory() - memBefore;
    }

    // Verify return value survives subsequent memory allocations
    function testValueSurvivesAllocation() public returns (uint memAfterCall, uint memAfterAlloc, uint value) {
        uint memBefore = freeMemory();
        value = impl.getUint();
        memAfterCall = freeMemory() - memBefore;  // Should be 0 (value type)
        // Allocate memory that overwrites returndata area
        bytes memory data = new bytes(64);
        data[0] = 0xff;
        memAfterAlloc = freeMemory() - memBefore;  // Should be 96 (32 len + 64 data)
        // value is still 42 because it's on the stack
    }

    // Verify memory doesn't grow with repeated calls
    function testNoMemoryGrowthInLoop() public returns (uint memDiff) {
        uint memBefore = freeMemory();
        for (uint i = 0; i < 10; i++) {
            impl.getUint();
        }
        memDiff = freeMemory() - memBefore;  // Should be 0, not 320 (10 * 32)
    }
}
// ====
// compileViaYul: true
// ----
// testBool() -> 0
// testUint() -> 0
// testAddress() -> 0
// testBytes32() -> 0
// testValueSurvivesAllocation() -> 0, 96, 42
// testNoMemoryGrowthInLoop() -> 0
