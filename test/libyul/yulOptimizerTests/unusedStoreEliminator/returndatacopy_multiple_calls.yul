{
    let s1 := returndatasize()
    pop(staticcall(gas(), 0x01, 0, 0, 0, 0))
    pop(staticcall(gas(), 0x02, 0, 0, 0, 0))
    let s2 := returndatasize()
    returndatacopy(0, 0, s1)
    returndatacopy(0, 0, s2)
}
// ====
// EVMVersion: >homestead
// ----
// step: unusedStoreEliminator
//
// {
//     {
//         let s1 := returndatasize()
//         pop(staticcall(gas(), 0x01, 0, 0, 0, 0))
//         pop(staticcall(gas(), 0x02, 0, 0, 0, 0))
//         let s2 := returndatasize()
//         returndatacopy(0, 0, s1)
//         let _17 := 0
//         let _18 := 0
//     }
// }
