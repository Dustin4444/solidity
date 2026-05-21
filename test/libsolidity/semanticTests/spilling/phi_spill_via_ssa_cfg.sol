// SPDX-License-Identifier: GPL-3.0
pragma solidity *;
contract C {
    function f(uint256[18] calldata x) external pure returns (uint256) {
        // Pre-load every input into a named local so the loop body has 18 live values plus
        // the loop counter `i` (a phi at the loop header). Touching every local in the body
        // forces them all live across the back-edge, so the via-SSA-CFG layout has to spill
        // — and the loop counter (a phi) is among the candidates the discovery picks.
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
        uint256 sum = 0;
        for (uint256 i = 0; i < a0; ++i) {
            unchecked {
                sum += a17 + a16 + a15 + a14 + a13 + a12 + a11 + a10
                     + a9 + a8 + a7 + a6 + a5 + a4 + a3 + a2 + a1 + i;
            }
        }
        return sum;
    }
}
// ====
// compileViaYul: true
// compileViaSSACFG: true
// experimental: true
// ----
// f(uint256[18]): 5, 100, 99, 98, 97, 96, 95, 94, 93, 92, 91, 90, 89, 88, 87, 86, 85, 84 -> 7830
