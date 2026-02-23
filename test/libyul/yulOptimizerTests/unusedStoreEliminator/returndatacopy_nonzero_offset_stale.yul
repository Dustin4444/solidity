{
    let s := returndatasize()
    pop(staticcall(gas(), 0x01, 0, 0, 0, 0))
    returndatacopy(10, 0, s)

    let s1 := returndatasize()
    returndatacopy(10, 0, s1)
}
// ====
// EVMVersion: >homestead
// ----
// step: unusedStoreEliminator
//
// {
//     {
//         let s := returndatasize()
//         pop(staticcall(gas(), 0x01, 0, 0, 0, 0))
//         returndatacopy(10, 0, s)
//         let s1 := returndatasize()
//         let _10 := 0
//         let _11 := 10
//     }
// }
