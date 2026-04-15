uint256 constant ONE = 1;
uint8 constant U8_64 = 64;
uint8 constant U8_255 = 255;

contract C {
    // Expression with only literals have rational type with unlimited precision,
    // so we use a integer constant to force the literal have uint256 (mobileType).
    // The whole expression then has type uint256.
    uint256 constant LITERAL_WRAP = 2**255 << ONE; // = 0
    uint[LITERAL_WRAP + 1] a;
    uint8 constant CONST_NO_WRAP = U8_64 << 1;
    uint[CONST_NO_WRAP] b;
    uint8 constant CONST_WRAP = U8_255 << 4; // = 240 (1111 0000)
    uint[CONST_WRAP] c;

    function testLiteralWrapEquivalence() public view returns (bool) {
        uint256 runTimeResult = 2**255 << ONE;

        return
                LITERAL_WRAP == runTimeResult &&
                a.length == runTimeResult + 1;
    }

    function testConstNoWrapEquivalence() public view returns (bool) {
        uint8 runTimeResult = U8_64 << 1;

        return
                CONST_NO_WRAP == runTimeResult &&
                b.length == runTimeResult;
    }

    function testConstWrapEquivalence() public view returns (bool) {
        uint8 runTimeResult = U8_255 << 4;

        return
                CONST_WRAP == runTimeResult &&
                c.length == runTimeResult;
    }
}
// ----
// testLiteralWrapEquivalence() -> true
// testConstNoWrapEquivalence() -> true
// testConstWrapEquivalence() -> true
