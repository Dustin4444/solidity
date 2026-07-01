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
	case 9 { sstore(0, 9) }
	case 10 { sstore(0, 10) }
	case 11 { sstore(0, 11) }
	case 12 { sstore(0, 12) }
	case 13 { sstore(0, 13) }
	case 14 { sstore(0, 14) }
	case 15 { sstore(0, 15) }
	case 16 { sstore(0, 16) }
}
// ----
// step: switchSplitter
//
// {
//     let _1 := 0
//     let sel := calldataload(_1)
//     switch gt(sel, 8)
//     case 1 {
//         switch gt(sel, 12)
//         case 1 {
//             switch sel
//             case 13 {
//                 let _26 := 13
//                 let _27 := 0
//                 sstore(_27, _26)
//             }
//             case 14 {
//                 let _28 := 14
//                 let _29 := 0
//                 sstore(_29, _28)
//             }
//             case 15 {
//                 let _30 := 15
//                 let _31 := 0
//                 sstore(_31, _30)
//             }
//             case 16 {
//                 let _32 := 16
//                 let _33 := 0
//                 sstore(_33, _32)
//             }
//         }
//         default {
//             switch sel
//             case 9 {
//                 let _18 := 9
//                 let _19 := 0
//                 sstore(_19, _18)
//             }
//             case 10 {
//                 let _20 := 10
//                 let _21 := 0
//                 sstore(_21, _20)
//             }
//             case 11 {
//                 let _22 := 11
//                 let _23 := 0
//                 sstore(_23, _22)
//             }
//             case 12 {
//                 let _24 := 12
//                 let _25 := 0
//                 sstore(_25, _24)
//             }
//         }
//     }
//     default {
//         switch gt(sel, 4)
//         case 1 {
//             switch sel
//             case 5 {
//                 let _10 := 5
//                 let _11 := 0
//                 sstore(_11, _10)
//             }
//             case 6 {
//                 let _12 := 6
//                 let _13 := 0
//                 sstore(_13, _12)
//             }
//             case 7 {
//                 let _14 := 7
//                 let _15 := 0
//                 sstore(_15, _14)
//             }
//             case 8 {
//                 let _16 := 8
//                 let _17 := 0
//                 sstore(_17, _16)
//             }
//         }
//         default {
//             switch sel
//             case 1 {
//                 let _2 := 1
//                 let _3 := 0
//                 sstore(_3, _2)
//             }
//             case 2 {
//                 let _4 := 2
//                 let _5 := 0
//                 sstore(_5, _4)
//             }
//             case 3 {
//                 let _6 := 3
//                 let _7 := 0
//                 sstore(_7, _6)
//             }
//             case 4 {
//                 let _8 := 4
//                 let _9 := 0
//                 sstore(_9, _8)
//             }
//         }
//     }
// }
