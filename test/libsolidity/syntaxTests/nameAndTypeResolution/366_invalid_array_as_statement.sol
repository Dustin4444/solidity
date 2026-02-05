contract test {
    struct S { uint x; }
    constructor(uint k) { S[k]; }
}
// ----
// TypeError 5462: (69-70): Invalid array length, expected integer literal or constant expression.
