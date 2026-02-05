interface I {}

contract C {
    function f() view public {
        I[0];
    }
}
// ----
// TypeError 1406: (70-71): Array with zero length specified.
