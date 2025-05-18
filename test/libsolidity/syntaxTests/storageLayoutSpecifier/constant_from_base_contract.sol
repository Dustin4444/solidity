contract A {
    uint constant x = 10;
}

contract C is A layout at A.x { }
// ----
