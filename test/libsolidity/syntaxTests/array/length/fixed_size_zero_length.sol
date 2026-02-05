struct S { uint[0] x; }

error Err(uint[0]);
event Ev(uint[0]);

contract C {
    int[0] a;
    uint[0] b;
    bytes1[0] c;
    bytes32[0] d;
    bytes[0] e;
    string[0] f;
    mapping(int => int[0]) m;
    function() returns (uint[0] memory) fPtr;

    function foo () private pure {
        uint[0] storage x;
    }

    function foo (uint[0] memory fArg) private pure returns(uint[0] memory)  {
        fArg;
    }

    function tup() private pure {
        (uint[0] memory aTup, uint bTup) = ([], 1);
    }

    function cat() private pure {
        try this.g() returns (uint[0] memory) {} catch (bytes memory bCatch) {}
    }

    function array_alloc() private pure {
        new uint[0][](3);
    }
}
// ----
// TypeError 1406: (16-17): Array with zero length specified.
// TypeError 1406: (40-41): Array with zero length specified.
// TypeError 1406: (59-60): Array with zero length specified.
// TypeError 1406: (86-87): Array with zero length specified.
// TypeError 1406: (101-102): Array with zero length specified.
// TypeError 1406: (118-119): Array with zero length specified.
// TypeError 1406: (136-137): Array with zero length specified.
// TypeError 1406: (152-153): Array with zero length specified.
// TypeError 1406: (169-170): Array with zero length specified.
// TypeError 1406: (198-199): Array with zero length specified.
// TypeError 1406: (234-235): Array with zero length specified.
// TypeError 1406: (300-301): Array with zero length specified.
// TypeError 1406: (344-345): Array with zero length specified.
// TypeError 1406: (386-387): Array with zero length specified.
// TypeError 1406: (469-470): Array with zero length specified.
// TypeError 1406: (583-584): Array with zero length specified.
// TypeError 1406: (694-695): Array with zero length specified.
