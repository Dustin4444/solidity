// SPDX-License-Identifier: GPL-3.0
// Yul equivalent of yul_optimizer_unused_store_eliminator_stale_returndatasize_bug/input.sol.
// Uses a self-call dispatcher so the test is executable: calling the contract with any
// selector other than "e1()" or "e()" runs f(), which reverts due to the out-of-bounds
// returndatacopy (returndatasize() == 0 after e() but s is stale == 32).
// The buggy UnusedStoreEliminator used to eliminate the returndatacopy, allowing
// the return(0, 0) to be reached instead of reverting.
object "C" {
    code {
        switch shr(224, calldataload(0))
        // "e1()" (0xa2c2d666 = bytes4(keccak256("e1()"))): returns 32 bytes.
        case 0xa2c2d666 {
            mstore(0, 1)
            return(0, 32)
        }
        // "e()" (0xffae15ba = bytes4(keccak256("e()"))): returns nothing.
        case 0xffae15ba {
            return(0, 0)
        }
        // "f()": demonstrates the stale-returndatasize bug.
        default {
            mstore(0x00, shl(0xe0, 0xa2c2d666))  // bytes4(keccak256("e1()"))
            pop(staticcall(gas(), address(), 0x00, 0x04, 0x00, 0x00))
            let s := returndatasize()            // s == 32 (return value of e1())
            mstore(0x00, shl(0xe0, 0xffae15ba))  // bytes4(keccak256("e()"))
            pop(staticcall(gas(), address(), 0x00, 0x04, 0x00, 0x00))
            // After e(), returndatasize() == 0, but s is stale (== 32).
            // The buggy optimizer removes this instruction; the fix preserves it.
            returndatacopy(0, 0x00, s)
            return(0, 0)
        }
    }
}
