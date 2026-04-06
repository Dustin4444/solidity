contract C {
    uint[] storageArray;
    function test_indices(uint256 len) public
    {
        while (storageArray.length < len)
            storageArray.push();
        while (storageArray.length > len)
            storageArray.pop();
        for (uint i = 0; i < len; i++)
            storageArray[i] = i + 1;

        for (uint i = 0; i < len; i++)
            require(storageArray[i] == i + 1);
    }
}
// ----
// test_indices(uint256): 1 ->
// test_indices(uint256): 129 ->
// gas irOptimized: 3017687
// gas legacy: 3038668
// gas legacyOptimized: 2995952
// test_indices(uint256): 5 ->
// gas irOptimized: 579670
// gas legacy: 573821
// gas legacyOptimized: 571837
// test_indices(uint256): 10 ->
// gas irOptimized: 157953
// gas legacy: 160122
// gas legacyOptimized: 156984
// test_indices(uint256): 15 ->
// gas irOptimized: 172733
// gas legacy: 175987
// gas legacyOptimized: 171584
// test_indices(uint256): 0xFF ->
// gas irOptimized: 5673823
// gas legacy: 5715762
// gas legacyOptimized: 5632544
// test_indices(uint256): 1000 ->
// gas irOptimized: 18173005
// gas legacy: 18347824
// gas legacyOptimized: 18037236
// test_indices(uint256): 129 ->
// gas irOptimized: 4166279
// gas legacy: 4140124
// gas legacyOptimized: 4108262
// test_indices(uint256): 128 ->
// gas irOptimized: 405522
// gas legacy: 433512
// gas legacyOptimized: 400897
// test_indices(uint256): 1 ->
// gas irOptimized: 583437
// gas legacy: 576726
// gas legacyOptimized: 575532
