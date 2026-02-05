contract B {
}

contract C is B {
    function f(bool[] calldata dd) pure public {
        super[1];
    }
}
// ----
// TypeError 5530: (91-99): Index notation is not allowed for type.
