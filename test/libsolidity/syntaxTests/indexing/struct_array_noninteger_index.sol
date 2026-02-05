contract test {
    struct s { uint a; uint b;}
    function f() pure public returns (bytes1) {
        s[75555555555555555555555555555555555555555555555555555555555555555555555555555555555555555555555555555555555555555555555555555555555555555555];
        s[7];
    }
}

// ----
// TypeError 1847: (106-246): Array length too large, maximum is 2**256 - 1.
