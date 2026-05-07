// SPDX-License-Identifier: GPL-3.0
pragma solidity *;

contract C {
    function e1() external pure returns(uint256) { return 1; }
    function e() external pure {}

    // rds captures returndatasize() after the call to e1() (which returns 32 bytes).
    // After the call to e() (which returns nothing), returndatasize() is 0, but rds
    // is stale (== 32). The buggy UnusedStoreEliminator used to eliminate the
    // returndatacopy as a dead store because it incorrectly treated rds as a live
    // returndatasize() value and assumed the copy could not go out of bounds.
    //
    // Destination offset 96 is chosen to make the written region [96, 128) provably
    // disjoint from the ABI encoder's mload(64) at [64, 96). With dest=0 the
    // optimizer conservatively treats the write as potentially overlapping mload(64)
    // and does not eliminate it even when the bug is present.
    function f() public view {
        assembly {
            mstore(0x00, shl(0xe0, 0xa2c2d666))  // bytes4(keccak256("e1()"))
            pop(staticcall(gas(), address(), 0x00, 0x04, 0x00, 0x00))
            let rds := returndatasize()           // rds == 32 (return value of e1())
            mstore(0x00, shl(0xe0, 0xffae15ba))  // bytes4(keccak256("e()"))
            pop(staticcall(gas(), address(), 0x00, 0x04, 0x00, 0x00))
            // After e(), returndatasize() == 0, but rds is stale (== 32).
            // The buggy optimizer removes this instruction; the fix preserves it.
            returndatacopy(96, 0x00, rds)
        }
    }
}
