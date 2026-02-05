interface I {}

library L {
    function f() pure public {
        I[1];
    }
}
// ----
// Warning 6133: (67-71): Statement has no effect.
