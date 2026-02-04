contract C {
    uint public i;
    constructor(uint newI) {
        i = newI;
    }
}
contract D {
    C c;
    constructor(uint v) {
        c = new C(v);
    }
    function f() public returns (uint r) {
        return c.i();
    }
}
// ----
// constructor(): 2 ->
// gas irOptimized: 138108
// gas irOptimized code: 43600
// gas legacy: 145302
// gas legacy code: 92600
// gas legacyOptimized: 138038
// gas legacyOptimized code: 51400
// f() -> 2
