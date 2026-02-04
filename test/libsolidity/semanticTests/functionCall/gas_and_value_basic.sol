contract helper {
    bool flag;

    function getBalance() public payable returns (uint256 myBalance) {
        return address(this).balance;
    }

    function setFlag() public {
        flag = true;
    }

    function getFlag() public returns (bool fl) {
        return flag;
    }
}
contract test {
    helper h;

    constructor() payable {
        h = new helper();
    }

    function sendAmount(uint256 amount) public payable returns (uint256 bal) {
        return h.getBalance{value: amount}();
    }

    function outOfGas() public returns (bool ret) {
        h.setFlag{gas: 2}(); // should fail due to OOG
        return true;
    }

    function checkState() public returns (bool flagAfter, uint256 myBal) {
        flagAfter = h.getFlag();
        myBal = address(this).balance;
    }
}
// ====
// bytecodeFormat: legacy
// ----
// constructor(), 20 wei ->
// gas irOptimized: 118852
// gas irOptimized code: 115000
// gas legacy: 130096
// gas legacy code: 255200
// gas legacyOptimized: 120567
// gas legacyOptimized code: 140800
// sendAmount(uint256): 5 -> 5
// outOfGas() -> FAILURE # call to helper should not succeed but amount should be transferred anyway #
// checkState() -> false, 15
