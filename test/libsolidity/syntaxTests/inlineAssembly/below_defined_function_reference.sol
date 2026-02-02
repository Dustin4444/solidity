contract C {
    function f(uint) external pure returns (uint) {
        assembly {
            mstore(0, id(calldataload(4)))
            return (0, 32)

            function id(x) -> y {
                y := x
            }
        }
    }
}