interface I {
    function getString() external returns (string memory);
    function getBytes() external returns (bytes memory);
    function getArray() external returns (uint256[] memory);
    function getStaticArray() external returns (uint[2] memory);
}

contract Impl is I {
    function getString() external pure returns (string memory) { return "hello"; }
    function getBytes() external pure returns (bytes memory) { return hex"1234"; }
    function getArray() external pure returns (uint256[] memory) {
        uint256[] memory arr = new uint256[](2);
        arr[0] = 1;
        arr[1] = 2;
        return arr;
    }
    function getStaticArray() external pure returns (uint[2] memory) {
        uint[2] memory arr;
        arr[0] = 1;
        arr[1] = 2;
        return arr;
    }
}

// Reference types allocate memory for both returndata buffer and decoded data.
contract C {
    I immutable impl = I(address(new Impl()));

    function freeMemory() internal pure returns (uint m) { assembly { m := mload(0x40) } }

    // 160 = returndata (32 + 32 + 32) + decoded (32 + 32)
    function testString() public returns (uint memDiff) {
        uint memBefore = freeMemory();
        impl.getString();
        memDiff = freeMemory() - memBefore;
    }

    // 160 = returndata (32 + 32 + 32) + decoded (32 + 32)
    function testBytes() public returns (uint memDiff) {
        uint memBefore = freeMemory();
        impl.getBytes();
        memDiff = freeMemory() - memBefore;
    }

    // 224 = returndata (32 + 32 + 64) + decoded (32 + 64)
    function testDynamicArray() public returns (uint memDiff) {
        uint memBefore = freeMemory();
        impl.getArray();
        memDiff = freeMemory() - memBefore;
    }

    // 128 = returndata (64) + decoded (64)
    function testStaticArray() public returns (uint memDiff) {
        uint memBefore = freeMemory();
        impl.getStaticArray();
        memDiff = freeMemory() - memBefore;
    }

    // Reference types do grow memory in loops
    // 1280 = 10 calls * 128 bytes per call
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
// compileViaYul: true
// ----
// testString() -> 160
// testBytes() -> 160
// testDynamicArray() -> 224
// testStaticArray() -> 128
// testMemoryGrowsInLoop() -> 1280
