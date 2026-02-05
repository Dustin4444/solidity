contract C {
    function f0() public {
        abi.decode("", (int[0]));
    }

    function f1() public {
        abi.decode("", (int[0][]));
    }

    function f2() public {
        abi.decode("", (int[][0]));
    }

    function f3() public {
        abi.decode("", (int[][0][]));
    }

    function f4() public {
        abi.decode("", (int[][][0]));
    }

    struct S { uint t; }
    function f5() public {
        abi.decode("", (S[][][0]));
    }
}
// ----
// TypeError 1406: (68-69): Array with zero length specified.
// TypeError 1406: (136-137): Array with zero length specified.
// TypeError 1406: (208-209): Array with zero length specified.
// TypeError 1406: (278-279): Array with zero length specified.
// TypeError 1406: (352-353): Array with zero length specified.
// TypeError 1406: (447-448): Array with zero length specified.
