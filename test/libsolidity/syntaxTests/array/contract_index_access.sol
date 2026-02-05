contract C {
    function f() pure public {
        C[1];
    }
}
// ----
// Warning 6133: (52-56): Statement has no effect.
