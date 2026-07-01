{
	let sel := calldataload(0)
	switch sel
	case 1 { sstore(0, 1) }
	case 2 { sstore(0, 2) }
	case 3 { sstore(0, 3) }
	case 4 { sstore(0, 4) }
	case 5 {
		let inner := calldataload(32)
		switch inner
		case 10 { sstore(1, 10) }
		case 11 { sstore(1, 11) }
		case 12 { sstore(1, 12) }
		case 13 { sstore(1, 13) }
		case 14 { sstore(1, 14) }
		case 15 { sstore(1, 15) }
		case 16 { sstore(1, 16) }
	}
	case 6 { sstore(0, 6) }
	case 7 { sstore(0, 7) }
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
//             let _10 := 32
//             let inner := calldataload(_10)
//             switch gt(inner, 13)
//             case 1 {
//                 switch inner
//                 case 14 {
//                     let _19 := 14
//                     let _20 := 1
//                     sstore(_20, _19)
//                 }
//                 case 15 {
//                     let _21 := 15
//                     let _22 := 1
//                     sstore(_22, _21)
//                 }
//                 case 16 {
//                     let _23 := 16
//                     let _24 := 1
//                     sstore(_24, _23)
//                 }
//             }
//             default {
//                 switch inner
//                 case 10 {
//                     let _11 := 10
//                     let _12 := 1
//                     sstore(_12, _11)
//                 }
//                 case 11 {
//                     let _13 := 11
//                     let _14 := 1
//                     sstore(_14, _13)
//                 }
//                 case 12 {
//                     let _15 := 12
//                     let _16 := 1
//                     sstore(_16, _15)
//                 }
//                 case 13 {
//                     let _17 := 13
//                     let _18 := 1
//                     sstore(_18, _17)
//                 }
//             }
//         }
//         case 6 {
//             let _25 := 6
//             let _26 := 0
//             sstore(_26, _25)
//         }
//         case 7 {
//             let _27 := 7
//             let _28 := 0
//             sstore(_28, _27)
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
