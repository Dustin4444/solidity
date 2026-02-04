contract C {
    function set(uint value) external {
        assembly {
            tstore(0, value)
        }
    }

    function get() external view returns (uint value) {
        assembly {
            value := tload(0)
        }
    }

    function terminate(address payable a) external {
        selfdestruct(a);
    }
}

contract D {
    C public c;

    constructor() {
        c = new C();
    }

    function destroy() external {
        c.set(42);
        c.terminate(payable(address(this)));
        assert(c.get() == 42);
    }

    function createAndDestroy() external {
        c = new C();
        c.set(42);
        c.terminate(payable(address(this)));
        assert(c.get() == 42);
    }
}
// ====
// EVMVersion: >=cancun
// bytecodeFormat: legacy
// ----
// constructor() ->
// gas irOptimized: 126462
// gas irOptimized code: 207200
// gas legacy: 148993
// gas legacy code: 495200
// gas legacyOptimized: 125590
// gas legacyOptimized code: 200200
// destroy() ->
// createAndDestroy() ->
// gas legacy: 67013
// gas legacy code: 92600
// gas legacyOptimized: 65639
// gas legacyOptimized code: 39400
