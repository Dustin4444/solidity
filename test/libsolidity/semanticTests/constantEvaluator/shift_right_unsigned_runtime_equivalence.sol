uint256 constant TWO = 2;
uint8 constant U8_128 = 128;
uint8 constant U8_240 = 240; // 1111 0000

contract C {
    uint256 constant LITERAL = 1 >> TWO;
    uint[LITERAL + 1] a;
    uint8 constant CONST = U8_240 >> 4;
    uint[CONST] b;
    uint constant CONST_BITS_DISCARDED = TWO >> 3;
    uint[CONST_BITS_DISCARDED + 1] c;

    function testLiteralWrapEquivalence() public view returns (bool) {
        uint256 runtimeResult = 1 >> TWO;
        return
            LITERAL == runtimeResult &&
            a.length == 1;
    }

    function testConstNoWrapEquivalence() public view returns (bool) {
        uint8 runtimeResult = U8_240 >> 4;
        return
            CONST == runtimeResult &&
            b.length == runtimeResult;
    }

    function testConstWrapEquivalence() public view returns (bool) {
        uint runtimeResult = TWO >> 3;
        return
            CONST_BITS_DISCARDED == runtimeResult &&
            c.length == 1;
    }
}
// ----
// testLiteralWrapEquivalence() -> true
// testConstNoWrapEquivalence() -> true
// testConstWrapEquivalence() -> true
