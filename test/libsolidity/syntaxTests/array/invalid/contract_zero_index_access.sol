contract C {
    function f() view public {
        C[0];
    }
}
// ----
// TypeError 1406: (54-55): Array with zero length specified.
