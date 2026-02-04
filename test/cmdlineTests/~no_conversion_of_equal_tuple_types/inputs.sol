// SPDX-License-Identifier: GPL-3.0
pragma solidity >= 0.0.0;

contract C {
    function f(uint[] calldata a) public pure returns(uint[] memory, uint[] calldata) {
        uint[] memory ops1;
        uint[] memory ops2;
        return true ? (ops1, a) : (ops2, a);
    }
}
