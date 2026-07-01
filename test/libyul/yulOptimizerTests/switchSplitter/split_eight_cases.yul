{
	let sel := calldataload(0)
	switch sel
	case 1 { sstore(0, 1) }
	case 2 { sstore(0, 2) }
	case 3 { sstore(0, 3) }
	case 4 { sstore(0, 4) }
	case 5 { sstore(0, 5) }
	case 6 { sstore(0, 6) }
	case 7 { sstore(0, 7) }
	case 8 { sstore(0, 8) }
}
// ----
// step: switchSplitter
//
// {
//     let _1 := 0
//     let sel := calldataload(_1)
//     switch gt(sel, 4)
//     case 1 {
//         switch sel
//         case 5 {
//             let _10 := 5
//             let _11 := 0
//             sstore(_11, _10)
//         }
//         case 6 {
//             let _12 := 6
//             let _13 := 0
//             sstore(_13, _12)
//         }
//         case 7 {
//             let _14 := 7
//             let _15 := 0
//             sstore(_15, _14)
//         }
//         case 8 {
//             let _16 := 8
//             let _17 := 0
//             sstore(_17, _16)
//         }
//     }
//     default {
//         switch sel
//         case 1 {
//             let _2 := 1
//             let _3 := 0
//             sstore(_3, _2)
//         }
//         case 2 {
//             let _4 := 2
//             let _5 := 0
//             sstore(_5, _4)
//         }
//         case 3 {
//             let _6 := 3
//             let _7 := 0
//             sstore(_7, _6)
//         }
//         case 4 {
//             let _8 := 4
//             let _9 := 0
//             sstore(_9, _8)
//         }
//     }
// }
