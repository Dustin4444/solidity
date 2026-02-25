contract B {
}

contract C is B {
    function f() pure public {
        super[1] memory t;
        t;
    }
}
// ----
// TypeError 5172: (73-78): Name has to refer to a user-defined type.
