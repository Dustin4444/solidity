{
    {
        mstore(memoryguard(0x010000), 1)
        sstore(mload(calldataload(0)), 1)
        let x := foo_s()
        mstore(192, x)
        let i := 0
        for { } lt(i, 0x60) { i := add(i, 0x20) }
        {
            switch 0x7fffffffffffff
            default { break }
        }
        sstore(foo_s(), foo_s())
    }
    function foo_s() -> x
    {
        let x_1 := x
        x_1 := call(0x4000000001, callcode(0x400000001, 0x40000001, callcode(xor(0x4000001, calldatasize()), 0x400001, 0x40001, mod(0x4001, 32768), mod(0x401, 32768), mod(0x41, 32768), mod(0x5, 32768)), mod(0x7, 32768), mod(0x7f, 32768), mod(calldatasize(), 32768), mod(calldatasize(), 32768)), 0x7ff, mod(0x7fff, 32768), mod(0x7ffff, 32768), mod(0x7fffff, 32768), mod(addmod(0x7ffffff, 0x7fffffff, 0x7ffffffff), 32768))
    }
}
// ----
// // object "object"
// // ===== SSA CFG =====
// memoryguard = 0x010000
//
// #0:
//     v0 = const 0x01
//     v1 = memoryguard
//     builtin @mstore v1, v0
//     v3 = const 0x00
//     v4 = builtin @calldataload v3
//     v5 = builtin @mload v4
//     builtin @sstore v5, v0
//     v7 = call @foo_s
//     v8 = const 0xc0
//     builtin @mstore v8, v7
//     v10 = const 0x60
//     v13 = const 0x7fffffffffffff
//     v14 = const 0x20
//     jump #1
// #1: preds: #0
//     v12 = builtin @lt v3, v10
//     branch v12, #2, #4
// #2: preds: #1
//     jump #4
// #4: preds: #1, #2
//     v19 = call @foo_s
//     v20 = call @foo_s
//     builtin @sstore v20, v19
//     main_exit
//
// func @foo_s(args: ()) -> 1 {
// #0:
//     v0 = const 0x00
//     v1 = const 0x8000
//     v2 = const 0x07ffffffff
//     v3 = const 0x7fffffff
//     v4 = const 0x07ffffff
//     v5 = builtin @addmod v4, v3, v2
//     v6 = builtin @mod v5, v1
//     v7 = const 0x7fffff
//     v8 = builtin @mod v7, v1
//     v9 = const 0x07ffff
//     v10 = builtin @mod v9, v1
//     v11 = const 0x7fff
//     v12 = builtin @mod v11, v1
//     v13 = const 0x07ff
//     v14 = builtin @calldatasize
//     v15 = builtin @mod v14, v1
//     v16 = builtin @calldatasize
//     v17 = builtin @mod v16, v1
//     v18 = const 0x7f
//     v19 = builtin @mod v18, v1
//     v20 = const 0x07
//     v21 = builtin @mod v20, v1
//     v22 = const 0x05
//     v23 = builtin @mod v22, v1
//     v24 = const 0x41
//     v25 = builtin @mod v24, v1
//     v26 = const 0x0401
//     v27 = builtin @mod v26, v1
//     v28 = const 0x4001
//     v29 = builtin @mod v28, v1
//     v30 = const 0x040001
//     v31 = const 0x400001
//     v32 = builtin @calldatasize
//     v33 = const 0x04000001
//     v34 = builtin @xor v33, v32
//     v35 = builtin @callcode v34, v31, v30, v29, v27, v25, v23
//     v36 = const 0x40000001
//     v37 = const 0x0400000001
//     v38 = builtin @callcode v37, v36, v35, v21, v19, v17, v15
//     v39 = const 0x4000000001
//     v40 = builtin @call v39, v38, v13, v12, v10, v8, v6
//     return v0
// }
//
// // ===== spill info =====
// // CFG[0] <main>
// //   spilled: none
// // CFG[1] foo_s
// //   spilled: none
// // ===== assembly =====
// tag_2:
//   0x010000
//   0x01
//   swap1
//   mstore
//   mload(calldataload(0x00))
//   0x01
//   swap1
//   sstore
//   tag_8
//   tag_1
//   jump	// in
// tag_8:
//   0xc0
//   mstore
//   jump(tag_3)
// tag_3:
//   jumpi(tag_4, lt(0x00, 0x60))
//   jump(tag_6)
// tag_6:
//   tag_9
//   tag_1
//   jump	// in
// tag_9:
//   tag_10
//   tag_1
//   jump	// in
// tag_10:
//   sstore
//   stop
// tag_4:
//   jump(tag_6)
// tag_1:
// tag_11:
//   addmod(0x07ffffff, 0x7fffffff, 0x07ffffffff)
//   0x8000
//   swap1
//   mod
//   mod(0x7fffff, 0x8000)
//   mod(0x07ffff, 0x8000)
//   mod(0x7fff, 0x8000)
//   calldatasize
//   0x8000
//   swap1
//   mod
//   calldatasize
//   0x8000
//   swap1
//   mod
//   mod(0x7f, 0x8000)
//   mod(0x07, 0x8000)
//   mod(0x05, 0x8000)
//   mod(0x41, 0x8000)
//   mod(0x0401, 0x8000)
//   mod(0x4001, 0x8000)
//   xor(0x04000001, calldatasize)
//   0x400001
//   0x040001
//   swap2
//   callcode
//   0x40000001
//   0x0400000001
//   callcode
//   0x07ff
//   swap1
//   0x4000000001
//   call
//   pop
//   0x00
//   swap1
//   jump	// out
