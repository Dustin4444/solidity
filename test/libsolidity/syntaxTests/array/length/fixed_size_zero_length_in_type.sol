contract C {
    function others() private pure {
        type(uint[0]);
    }
}
// ----
// TypeError 1406: (68-69): Array with zero length specified.
// TypeError 4259: (63-70): Invalid type for argument in the function call. An enum type, contract type or an integer type is required, but type(uint256[0] memory) provided.
