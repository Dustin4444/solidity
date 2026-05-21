{
    // Function uses every one of its 18 parameters at the bottom (the final sstore
    // chain). Each parameter is needed by at least one operation across the whole body,
    // so the layout can't drop any until the very end. The shuffler can't fit all 18
    // within swap range; the discovery loop spills the deepest unreachable ones, and
    // the spill set lands on `SSACFGStackLayout::spillSet`.
    function f(b1, b2, b3, b4, b5, b6, b7, b8, b9, b10, b11, b12, b13, b14, b15, b16, b17, b18) {
        sstore(0, b18)
        sstore(1, b17)
        sstore(2, b16)
        sstore(3, b15)
        sstore(4, b14)
        sstore(5, b13)
        sstore(6, b12)
        sstore(7, b11)
        sstore(8, b10)
        sstore(9, b9)
        sstore(10, b8)
        sstore(11, b7)
        sstore(12, b6)
        sstore(13, b5)
        sstore(14, b4)
        sstore(15, b3)
        sstore(16, b2)
        sstore(17, b1)
    }
    f(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18)
}
// ----
// digraph SSACFG {
// nodesep=0.7;
// graph[fontname="DejaVu Sans", rankdir=LR]
// node[shape=box,fontname="DejaVu Sans"];
//
// Entry [label="Entry
// spilled: {}"];
// Entry -> Block0_0;
// Block0_0 [label="\
// IN: []\l\
// \l\
// [FunctionCallReturnLabel[0], lit0, lit1, lit2, lit3, lit4, lit5, lit6, lit7, lit8, lit9, lit10, lit11, lit12, lit13, lit14, lit15, lit16, lit17]\l\
// f\l\
// [FunctionCallReturnLabel[0]]\l\
// \l\
// OUT: []\l\
// "];
// Block0_0Exit [label="MainExit"];
// Block0_0 -> Block0_0Exit;
// FunctionEntry_f_0 [label="function f:
//  f(v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14, v15, v16, v17)
// spilled: {v0}"];
// FunctionEntry_f_0 -> Block1_0;
// Block1_0 [label="\
// IN: [ReturnLabel[1], v17, v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, v0]\l\
// \l\
// [ReturnLabel[1], v1, v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v17, v17, lit18]\l\
// sstore\l\
// [ReturnLabel[1], v1, v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v17]\l\
// \l\
// [ReturnLabel[1], v1, v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v16, lit20]\l\
// sstore\l\
// [ReturnLabel[1], v1, v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2]\l\
// \l\
// [ReturnLabel[1], v1, JUNK, v2, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v15, lit22]\l\
// sstore\l\
// [ReturnLabel[1], v1, JUNK, v2, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3]\l\
// \l\
// [ReturnLabel[1], v1, JUNK, v2, v3, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v14, lit24]\l\
// sstore\l\
// [ReturnLabel[1], v1, JUNK, v2, v3, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4]\l\
// \l\
// [ReturnLabel[1], v1, JUNK, v2, v3, v4, v12, v11, v10, v9, v8, v7, v6, v5, v13, lit26]\l\
// sstore\l\
// [ReturnLabel[1], v1, JUNK, v2, v3, v4, v12, v11, v10, v9, v8, v7, v6, v5]\l\
// \l\
// [ReturnLabel[1], v1, JUNK, v2, v3, v4, v5, v11, v10, v9, v8, v7, v6, v12, lit28]\l\
// sstore\l\
// [ReturnLabel[1], v1, JUNK, v2, v3, v4, v5, v11, v10, v9, v8, v7, v6]\l\
// \l\
// [ReturnLabel[1], v1, JUNK, v2, v3, v4, v5, v6, v10, v9, v8, v7, v11, lit30]\l\
// sstore\l\
// [ReturnLabel[1], v1, JUNK, v2, v3, v4, v5, v6, v10, v9, v8, v7]\l\
// \l\
// [ReturnLabel[1], v1, JUNK, v2, v3, v4, v5, v6, v7, v9, v8, v10, lit32]\l\
// sstore\l\
// [ReturnLabel[1], v1, JUNK, v2, v3, v4, v5, v6, v7, v9, v8]\l\
// \l\
// [ReturnLabel[1], v1, JUNK, v2, v3, v4, v5, v6, v7, v8, v9, lit34]\l\
// sstore\l\
// [ReturnLabel[1], v1, JUNK, v2, v3, v4, v5, v6, v7, v8]\l\
// \l\
// [ReturnLabel[1], v1, JUNK, v2, v3, v4, v5, v6, v7, v8, lit36]\l\
// sstore\l\
// [ReturnLabel[1], v1, JUNK, v2, v3, v4, v5, v6, v7]\l\
// \l\
// [ReturnLabel[1], v1, JUNK, v2, v3, v4, v5, v6, v7, lit38]\l\
// sstore\l\
// [ReturnLabel[1], v1, JUNK, v2, v3, v4, v5, v6]\l\
// \l\
// [ReturnLabel[1], v1, JUNK, v2, v3, v4, v5, v6, lit40]\l\
// sstore\l\
// [ReturnLabel[1], v1, JUNK, v2, v3, v4, v5]\l\
// \l\
// [ReturnLabel[1], v1, JUNK, v2, v3, v4, v5, lit42]\l\
// sstore\l\
// [ReturnLabel[1], v1, JUNK, v2, v3, v4]\l\
// \l\
// [ReturnLabel[1], v1, JUNK, v2, v3, v4, lit44]\l\
// sstore\l\
// [ReturnLabel[1], v1, JUNK, v2, v3]\l\
// \l\
// [ReturnLabel[1], v1, JUNK, v2, v3, lit46]\l\
// sstore\l\
// [ReturnLabel[1], v1, JUNK, v2]\l\
// \l\
// [ReturnLabel[1], v1, JUNK, v2, lit48]\l\
// sstore\l\
// [ReturnLabel[1], v1, JUNK]\l\
// \l\
// [ReturnLabel[1], v1, v1, lit50]\l\
// sstore\l\
// [ReturnLabel[1], v1]\l\
// \l\
// [ReturnLabel[1], JUNK, v0, lit52]\l\
// sstore\l\
// [ReturnLabel[1], JUNK]\l\
// \l\
// OUT: [ReturnLabel[1]]\l\
// "];
// Block1_0Exit [label="FunctionReturn[]"];
// Block1_0 -> Block1_0Exit;
// // Spilled[1]: {v0}
// }
