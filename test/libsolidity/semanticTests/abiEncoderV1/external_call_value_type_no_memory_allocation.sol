pragma abicoder v1;

interface I {
    function getUint() external returns (uint256);
}

contract Impl is I {
    function getUint() external pure returns (uint256) { return 42; }
}

contract C {
    I immutable impl = I(address(new Impl()));

    function freeMemory() internal pure returns (uint m) { assembly { m := mload(0x40) } }

    function testValueType() public returns (uint memDiff) {
        uint memBefore = freeMemory();
        impl.getUint();
        memDiff = freeMemory() - memBefore;
    }

    function testNoMemoryGrowthInLoop() public returns (uint memDiff) {
        uint memBefore = freeMemory();
        for (uint i = 0; i < 10; i++) {
            impl.getUint();
        }
        memDiff = freeMemory() - memBefore;
    }
}
// ====
// compileViaYul: false
// ----
// testValueType() -> 0
// testNoMemoryGrowthInLoop() -> 0
