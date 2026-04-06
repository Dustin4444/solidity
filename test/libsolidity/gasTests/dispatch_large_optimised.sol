contract Large {
    uint public a;
    uint[] public b;
    function f1(uint x) public returns (uint) { a = x; b[uint8(msg.data[0])] = x; }
    function f2(uint x) public returns (uint) { b[uint8(msg.data[1])] = x; }
    function f3(uint x) public returns (uint) { b[uint8(msg.data[2])] = x; }
    function f4(uint x) public returns (uint) { b[uint8(msg.data[3])] = x; }
    function f5(uint x) public returns (uint) { b[uint8(msg.data[4])] = x; }
    function f6(uint x) public returns (uint) { b[uint8(msg.data[5])] = x; }
    function f7(uint x) public returns (uint) { b[uint8(msg.data[6])] = x; }
    function f8(uint x) public returns (uint) { b[uint8(msg.data[7])] = x; }
    function f9(uint x) public returns (uint) { b[uint8(msg.data[8])] = x; }
    function f0(uint x) public pure returns (uint) { require(x > 10); }
    function g1(uint x) public payable returns (uint) { a = x; b[uint8(msg.data[0])] = x; }
    function g2(uint x) public payable returns (uint) { b[uint8(msg.data[1])] = x; }
    function g3(uint x) public payable returns (uint) { b[uint8(msg.data[2])] = x; }
    function g4(uint x) public payable returns (uint) { b[uint8(msg.data[3])] = x; }
    function g5(uint x) public payable returns (uint) { b[uint8(msg.data[4])] = x; }
    function g6(uint x) public payable returns (uint) { b[uint8(msg.data[5])] = x; }
    function g7(uint x) public payable returns (uint) { b[uint8(msg.data[6])] = x; }
    function g8(uint x) public payable returns (uint) { b[uint8(msg.data[7])] = x; }
    function g9(uint x) public payable returns (uint) { b[uint8(msg.data[8])] = x; }
    function g0(uint x) public payable returns (uint) { require(x > 10); }
}
// ====
// EVMVersion: =current
// bytecodeFormat: legacy
// optimize: true
// optimize-runs: 2
// ----
// creation:
//   codeDepositCost: 213600
//   executionCost: 255
//   totalCost: 213855
// external:
//   a(): 2281
//   b(uint256): 4922
//   f0(uint256): 351
//   f1(uint256): 46990
//   f2(uint256): 24955
//   f3(uint256): 25043
//   f4(uint256): 25021
//   f5(uint256): 24999
//   f6(uint256): 24911
//   f7(uint256): 24691
//   f8(uint256): 24823
//   f9(uint256): 24845
//   g0(uint256): 591
//   g1(uint256): 46702
//   g2(uint256): 24689
//   g3(uint256): 24777
//   g4(uint256): 24755
//   g5(uint256): 24843
//   g6(uint256): 24623
//   g7(uint256): 24733
//   g8(uint256): 24711
//   g9(uint256): 24557
