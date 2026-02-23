{
    let s := returndatasize()
    pop(call(gas(), 0x01, 0, 0, 0, 0, 0))
    pop(delegatecall(gas(), 0x02, 0, 0, 0, 0))
    pop(staticcall(gas(), 0x03, 0, 0, 0, 0))
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
//         pop(call(gas(), 0x01, 0, 0, 0, 0, 0))
//         pop(delegatecall(gas(), 0x02, 0, 0, 0, 0))
//         pop(staticcall(gas(), 0x03, 0, 0, 0, 0))
//         returndatacopy(0, 0, s)
//     }
// }
