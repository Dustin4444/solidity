// This test ensures that `returndatacopy` is NOT optimized away.
{
  let s := returndatasize()
  returndatacopy(0,0,s)
}
// ====
// EVMVersion: >homestead
// ----
// step: unusedStoreEliminator
//
// {
//     {
//         returndatacopy(0, 0, returndatasize())
//     }
// }
