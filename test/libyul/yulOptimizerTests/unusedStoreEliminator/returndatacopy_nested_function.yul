{
    function helper() -> size {
        size := returndatasize()
    }

    let s := helper()
    pop(staticcall(gas(), 0x01, 0, 0, 0, 0))
    returndatacopy(0, 0, s)

    let s1 := helper()
    returndatacopy(0, 0, s1)
}
// ====
// EVMVersion: >homestead
// ----
// step: unusedStoreEliminator
//
// {
//     {
//         let s := helper()
//         pop(staticcall(gas(), 0x01, 0, 0, 0, 0))
//         returndatacopy(0, 0, s)
//         returndatacopy(0, 0, helper())
//     }
//     function helper() -> size
//     {
//         size := returndatasize()
//         let size_12 := size
//     }
// }
