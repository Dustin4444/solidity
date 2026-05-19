{
    let x := calldataload(0)
    let a := shl(12, shr(4, x))
    let b := shl(4, shr(12, x))
    let c := shr(12, shl(4, x))
    let d := shr(4, shl(12, x))
    let e := shl(150, shr(2, shl(150, x)))
    sstore(15, x)
    sstore(16, a)
    sstore(17, b)
    sstore(18, c)
    sstore(19, d)
    sstore(20, e)
}
// ====
// EVMVersion: >byzantium
// ----
// step: fullSuite
//
// {
//     {
//         let x := calldataload(0)
//         let _1 := shl(8, x)
//         let _2 := shr(8, x)
//         sstore(15, x)
//         sstore(16, and(_1, 0xfffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff000))
//         sstore(17, and(_2, 0xfffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff0))
//         sstore(18, and(_2, 0x0fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff))
//         sstore(19, and(_1, 0x0fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff00))
//         sstore(20, 0)
//     }
// }
