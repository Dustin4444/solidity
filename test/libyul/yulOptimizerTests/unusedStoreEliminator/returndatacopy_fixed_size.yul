// Unused `returndatacopy()` is not supposed be optimized out in this case.
// In fact, it's never optimized out now that we removed this optimization from UnusedStoreEliminator.
{
  returndatacopy(0,0,32)
}
// ====
// EVMVersion: >homestead
// ----
// step: unusedStoreEliminator
//
// { { returndatacopy(0, 0, 32) } }
