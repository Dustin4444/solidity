// This optimisation step is removed and `returndatacopy` is never removed by `unusedStoreEliminator`.
{
  returndatacopy(0,1,returndatasize())
}
// ====
// EVMVersion: >homestead
// ----
// step: unusedStoreEliminator
//
// {
//     {
//         returndatacopy(0, 1, returndatasize())
//     }
// }
