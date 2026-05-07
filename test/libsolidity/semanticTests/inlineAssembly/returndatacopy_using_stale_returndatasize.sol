contract C {
    function e1() external returns(uint256) {
        return 1;
    }

    function e() external {}

    // This test verifies that UnusedStoreEliminator does not incorrectly eliminate
    // a returndatacopy whose size argument is a stale snapshot of returndatasize()
    // taken before a subsequent call that resets the return data buffer.
    //
    // The test only detects the bug when run with --optimize.
    //
    // The destination offset 96 is chosen so that the written region [96, 128)
    // is provably disjoint from the ABI encoder's mload(64) at [64, 96).
    function f() public {
        assembly {
            mstore(0x00, shl(0xe0, 0xa2c2d666))  // bytes4(keccak256("e1()"))
            pop(staticcall(gas(), address(), 0x00, 0x04, 0x00, 0x00))
            let rds := returndatasize()  // rds == 32 (return value of e1())
            mstore(0x00, shl(0xe0, 0xffae15ba))  // bytes4(keccak256("e()"))
            pop(staticcall(gas(), address(), 0x00, 0x04, 0x00, 0x00))
            // After e(), returndatasize() == 0, but rds is stale (== 32).
            // The out-of-bounds access reverts.
            returndatacopy(96, 0x00, rds)
        }
    }
}
// ====
// EVMVersion: >=constantinople
// ----
// f() -> FAILURE
