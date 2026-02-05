contract C {
    function conditional() private pure {
        true ? uint[0] : uint[0];
    }
}
// ----
// TypeError 1406: (75-76): Array with zero length specified.
// TypeError 1406: (85-86): Array with zero length specified.
// TypeError 9717: (70-77): Invalid mobile type in true expression.
// TypeError 3703: (80-87): Invalid mobile type in false expression.
