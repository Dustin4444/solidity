contract Small {
    uint public a;
    uint[] public b;
    function f1(uint x) public returns (uint) { a = x; b[uint8(msg.data[0])] = x; }
    fallback () external payable {}
}
// ====
// EVMVersion: =current
// bytecodeFormat: legacy
// optimize: true
// optimize-runs: 2
// ----
// creation:
//   codeDepositCost: 56200
//   executionCost: 103
//   totalCost: 56303
// external:
//   fallback: 117
//   a(): 2259
//   b(uint256): 4570
//   f1(uint256): 46704
