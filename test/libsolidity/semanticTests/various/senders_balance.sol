contract C {
    function f() public view returns (uint256) {
        return msg.sender.balance;
    }
}


contract D {
    C c = new C();

    constructor() payable {}

    function f() public view returns (uint256) {
        return c.f();
    }
}
// ----
// constructor(), 27 wei ->
// gas irOptimized: 113229
// gas irOptimized code: 43600
// gas legacy: 117564
// gas legacy code: 97600
// gas legacyOptimized: 113417
// gas legacyOptimized code: 50400
// f() -> 27
