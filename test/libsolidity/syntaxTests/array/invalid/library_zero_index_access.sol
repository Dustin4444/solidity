library L {
    function f() view public {
        L[0];
    }
}
// ----
// TypeError 2876: (51-55): Index access for library types and arrays of libraries are not possible.
// TypeError 1406: (53-54): Array with zero length specified.
