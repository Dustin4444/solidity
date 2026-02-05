type MyInt is int;
struct S { uint i; }
enum E { ONE, TWO }

contract C {
    function f() pure public {
        MyInt[1];
        address[1];
        int[1];
        bytes1[1];
        S[1];
        S[1][1];
        E[1];
        bool[1];
        C[1];
    }
}
// ----
// Warning 6133: (113-121): Statement has no effect.
// Warning 6133: (131-141): Statement has no effect.
// Warning 6133: (151-157): Statement has no effect.
// Warning 6133: (167-176): Statement has no effect.
// Warning 6133: (186-190): Statement has no effect.
// Warning 6133: (200-207): Statement has no effect.
// Warning 6133: (217-221): Statement has no effect.
// Warning 6133: (231-238): Statement has no effect.
// Warning 6133: (248-252): Statement has no effect.
