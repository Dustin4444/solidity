{
    returndatacopy(0, 0, returndatasize())

    let s := returndatasize()
    returndatacopy(0, 0, s)
}
// ====
// EVMVersion: >homestead
// ----
// step: unusedStoreEliminator
//
// {
//     {
//         let _1 := returndatasize()
//         let _2 := 0
//         let _3 := 0
//         let s := returndatasize()
//         let _4 := 0
//         let _5 := 0
//     }
// }
