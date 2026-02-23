{
    let s := returndatasize()
    if iszero(0) {
        pop(staticcall(gas(), 0x01, 0, 0, 0, 0))
    }
    returndatacopy(0, 0, s)
}
// ====
// EVMVersion: >homestead
// ----
// step: unusedStoreEliminator
//
// {
//     {
//         let s := returndatasize()
//         if iszero(0)
//         {
//             pop(staticcall(gas(), 0x01, 0, 0, 0, 0))
//         }
//         returndatacopy(0, 0, s)
//     }
// }
