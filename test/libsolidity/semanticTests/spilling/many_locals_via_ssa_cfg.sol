// SPDX-License-Identifier: GPL-3.0
pragma solidity *;
contract C {
    function f(uint256[20] calldata x) external pure returns (uint256) {
        // Deliberately read every element into a named local so all 20 stay live across the
        // sum below. Without spilling, the via-SSA-CFG pipeline rejects this with stack-too-deep;
        // with spilling, the deepest locals are written to the reserved memory region and
        // brought back via mload as needed. The legacy pipeline already handles this via
        // StackLimitEvader.
        uint256 a0 = x[0];
        uint256 a1 = x[1];
        uint256 a2 = x[2];
        uint256 a3 = x[3];
        uint256 a4 = x[4];
        uint256 a5 = x[5];
        uint256 a6 = x[6];
        uint256 a7 = x[7];
        uint256 a8 = x[8];
        uint256 a9 = x[9];
        uint256 a10 = x[10];
        uint256 a11 = x[11];
        uint256 a12 = x[12];
        uint256 a13 = x[13];
        uint256 a14 = x[14];
        uint256 a15 = x[15];
        uint256 a16 = x[16];
        uint256 a17 = x[17];
        uint256 a18 = x[18];
        uint256 a19 = x[19];
        unchecked {
            return a19 + a18 + a17 + a16 + a15 + a14 + a13 + a12 + a11 + a10
                 + a9 + a8 + a7 + a6 + a5 + a4 + a3 + a2 + a1 + a0;
        }
    }
}
// ====
// compileViaYul: true
// compileViaSSACFG: true
// experimental: true
// ----
// f(uint256[20]): 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20 -> 210
