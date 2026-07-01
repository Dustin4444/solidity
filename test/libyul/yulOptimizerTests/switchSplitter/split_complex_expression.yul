{
	switch add(calldataload(0), 1)
	case 1 { sstore(0, 1) }
	case 2 { sstore(0, 2) }
	case 3 { sstore(0, 3) }
	case 4 { sstore(0, 4) }
	case 5 { sstore(0, 5) }
	case 6 { sstore(0, 6) }
	case 7 { sstore(0, 7) }
}
// ----
// step: switchSplitter
//
// {
//     let _1 := 1
//     let _2 := 0
//     let _3 := calldataload(_2)
//     let _4 := add(_3, _1)
//     switch gt(_4, 4)
//     case 1 {
//         switch _4
//         case 5 {
//             let _13 := 5
//             let _14 := 0
//             sstore(_14, _13)
//         }
//         case 6 {
//             let _15 := 6
//             let _16 := 0
//             sstore(_16, _15)
//         }
//         case 7 {
//             let _17 := 7
//             let _18 := 0
//             sstore(_18, _17)
//         }
//     }
//     default {
//         switch _4
//         case 1 {
//             let _5 := 1
//             let _6 := 0
//             sstore(_6, _5)
//         }
//         case 2 {
//             let _7 := 2
//             let _8 := 0
//             sstore(_8, _7)
//         }
//         case 3 {
//             let _9 := 3
//             let _10 := 0
//             sstore(_10, _9)
//         }
//         case 4 {
//             let _11 := 4
//             let _12 := 0
//             sstore(_12, _11)
//         }
//     }
// }
