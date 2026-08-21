contract StripReasonPanic {

    function requireReasonIsEvaluated() external returns (uint256) {
        require(true, (new bytes(0))[0] == bytes1(0) ? "first" : "second");
        return 1;
    }

    function revertReasonIsEvaluated() external returns (uint256) {
        revert((new bytes(0))[0] == bytes1(0) ? "first" : "second");
    }
}
// ====
// revertStrings: strip
// ----
// requireReasonIsEvaluated() -> FAILURE, hex"4e487b71", 0x32
// revertReasonIsEvaluated() -> FAILURE, hex"4e487b71", 0x32
