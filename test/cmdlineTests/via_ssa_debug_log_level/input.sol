// SPDX-License-Identifier: GPL-3.0
pragma solidity >=0.0;

contract C {
    uint x;
    function f(uint y) public returns (uint) {
        x = 1;
        for (uint i = 0; i < y; ++i)
            x = x * x;

        return x;
    }
}
