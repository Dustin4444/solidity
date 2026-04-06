contract Medium {
    uint public a;
    uint[] public b;
    function f1(uint x) public returns (uint) { a = x; b[uint8(msg.data[0])] = x; }
    function f2(uint x) public returns (uint) { b[uint8(msg.data[1])] = x; }
    function f3(uint x) public returns (uint) { b[uint8(msg.data[2])] = x; }
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
//   codeDepositCost: 118000
//   executionCost: 163
//   totalCost: 118163
// external:
//   a(): 2281
//   b(uint256): 4680
//   f1(uint256): 46770
//   f2(uint256): 24713
//   f3(uint256): 24757
//   g0(uint256): 349
//   g7(uint256): 24623
//   g8(uint256): 24601
//   g9(uint256): 24557
