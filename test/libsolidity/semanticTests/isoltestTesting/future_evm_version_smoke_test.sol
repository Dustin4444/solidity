contract C {
    uint256 x = 10;

    function getX() external view returns (uint256) {
        return x;
    }
}
// ====
// EVMVersion: =@future
// experimental: true
// ----
// getX() -> 10
