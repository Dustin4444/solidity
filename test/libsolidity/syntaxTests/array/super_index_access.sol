contract B {
}

contract C is B {
    function f() pure public {
        super[1];
    }
}
// ----
