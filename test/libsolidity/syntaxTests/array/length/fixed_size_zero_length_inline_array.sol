contract C {
    function others() private pure {
        [uint[0]][0];
    }
}
// ----
// TypeError 1406: (64-65): Array with zero length specified.
// TypeError 9563: (59-66): Invalid mobile type.
