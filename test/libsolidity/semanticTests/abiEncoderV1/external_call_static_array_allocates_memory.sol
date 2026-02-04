pragma abicoder v1;

interface I {
    function getStaticArray() external returns (uint[2] memory);
}

contract Impl is I {
    function getStaticArray() external pure returns (uint[2] memory) {
        uint[2] memory arr;
        arr[0] = 1;
        arr[1] = 2;
        return arr;
    }
}

// With ABIEncoderV1, static arrays allocate memory for returndata buffer.
// The decoded array is in place, so only returndata buffer is allocated.
contract C {
    I immutable impl = I(address(new Impl()));

    function freeMemory() internal pure returns (uint m) { assembly { m := mload(0x40) } }

    // 64 = returndata buffer only (decoded in place)
    function testStaticArray() public returns (uint memDiff) {
        uint memBefore = freeMemory();
        impl.getStaticArray();
        memDiff = freeMemory() - memBefore;
    }

    // 640 = 10 calls * 64 bytes per call
    function testMemoryGrowsInLoop() public returns (uint memDiff) {
        uint memBefore = freeMemory();
        for (uint i = 0; i < 10; i++) {
            impl.getStaticArray();
        }
        memDiff = freeMemory() - memBefore;
    }
}
// ====
// EVMVersion: >=byzantium
// compileViaYul: false
// ----
// testStaticArray() -> 64
// testMemoryGrowsInLoop() -> 640
