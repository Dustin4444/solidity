contract C {
    function f() public pure
    {
        // RLO
        /** overflow ‮ */
    }
}
// ----
// ParserError 8936: (71-90): Mismatching directional override markers in comment or string literal.
