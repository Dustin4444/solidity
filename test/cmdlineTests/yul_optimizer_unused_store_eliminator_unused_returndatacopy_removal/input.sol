// SPDX-License-Identifier: GPL-3.0
pragma solidity *;

contract C {
    function f() public pure {
        assembly {
            // Destination offset 96 is chosen to make the written memory region [96, 96+returndatasize())
            // provably disjoint from the ABI encoder's mload(64) (which reads [64, 96)).
            // `returndatacopy` used to be removed but now it stays, because this optimisation was removed.
            returndatacopy(96, 0, returndatasize())
        }
    }
}
