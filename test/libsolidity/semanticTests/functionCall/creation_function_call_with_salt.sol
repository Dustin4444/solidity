contract C {
    uint public i;
    constructor(uint newI) {
        i = newI;
    }
}
contract D {
    C c;
    constructor(uint v) {
        c = new C{salt: "abc"}(v);
    }
    function f() public returns (uint r) {
        return c.i();
    }
}
// ====
// EVMVersion: >=constantinople
// ----
// constructor(): 2 ->
// gas irOptimized: 138290
// gas irOptimized code: 43600
// gas legacy: 145668
// gas legacy code: 92600
// gas legacyOptimized: 138270
// gas legacyOptimized code: 51400
// f() -> 2
