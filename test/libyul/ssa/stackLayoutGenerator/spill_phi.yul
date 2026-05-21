{
    // Forces a phi value to land in the spill set: every parameter has to stay live
    // through the loop body's sstore chain (so they are all live-out of the header
    // and the body), and the loop counter `i` is itself a phi at the header. With
    // 17 params + the phi all live across the body, the shuffler can't fit them in
    // swap range and must spill at least one — and the phi can be among the
    // culprits depending on layout choices.
    function f(b1, b2, b3, b4, b5, b6, b7, b8, b9, b10, b11, b12, b13, b14, b15, b16, b17) {
        for { let i := 0 } lt(i, 5) { i := add(i, 1) } {
            sstore(i, b17)
            sstore(add(i, 1), b16)
            sstore(add(i, 2), b15)
            sstore(add(i, 3), b14)
            sstore(add(i, 4), b13)
            sstore(add(i, 5), b12)
            sstore(add(i, 6), b11)
            sstore(add(i, 7), b10)
            sstore(add(i, 8), b9)
            sstore(add(i, 9), b8)
            sstore(add(i, 10), b7)
            sstore(add(i, 11), b6)
            sstore(add(i, 12), b5)
            sstore(add(i, 13), b4)
            sstore(add(i, 14), b3)
            sstore(add(i, 15), b2)
            sstore(add(i, 16), b1)
        }
    }
    f(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17)
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
// [FunctionCallReturnLabel[0], lit0, lit1, lit2, lit3, lit4, lit5, lit6, lit7, lit8, lit9, lit10, lit11, lit12, lit13, lit14, lit15, lit16]\l\
// f\l\
// [FunctionCallReturnLabel[0]]\l\
// \l\
// OUT: []\l\
// "];
// Block0_0Exit [label="MainExit"];
// Block0_0 -> Block0_0Exit;
// FunctionEntry_f_0 [label="function f:
//  f(v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14, v15, v16)
// spilled: {v0, v19}"];
// FunctionEntry_f_0 -> Block1_0;
// Block1_0 [label="\
// IN: [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, v0]\l\
// \l\
// OUT: [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, v0]\l\
// "];
// Block1_0 -> Block1_0Exit [arrowhead=none];
// Block1_0Exit [label="Jump" shape=oval];
// Block1_0Exit -> Block1_1 [style="solid"];
// Block1_1 [label="\
// IN: [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, v0, phi19]\l\
// \l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, v0, phi19, lit18, phi19]\l\
// lt\l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, v0, phi19, v20]\l\
// \l\
// OUT: [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, v0, phi19, v20]\l\
// "];
// Block1_1 -> Block1_1Exit;
// Block1_1Exit [label="{ If v20 | { <0> Zero | <1> NonZero }}" shape=Mrecord];
// Block1_1Exit:0 -> Block1_4 [style="solid"];
// Block1_1Exit:1 -> Block1_2 [style="solid"];
// Block1_2 [label="\
// IN: [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, v0, phi19]\l\
// \l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, v16, v16, v16, phi19]\l\
// sstore\l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, v16, v16]\l\
// \l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, v16, v16, lit24, phi19]\l\
// add\l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, v16, v16, v25]\l\
// \l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, v25, v15, v15, v25]\l\
// sstore\l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, v25, v15]\l\
// \l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15, lit28, phi19]\l\
// add\l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15, v29]\l\
// \l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, v29, v15, v14, v29]\l\
// sstore\l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, v29, v15]\l\
// \l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15, lit32, phi19]\l\
// add\l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15, v33]\l\
// \l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15, v13, v33]\l\
// sstore\l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15]\l\
// \l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15, lit36, phi19]\l\
// add\l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15, v37]\l\
// \l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15, v12, v37]\l\
// sstore\l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15]\l\
// \l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15, lit18, phi19]\l\
// add\l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15, v40]\l\
// \l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15, v11, v40]\l\
// sstore\l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15]\l\
// \l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15, lit43, phi19]\l\
// add\l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15, v44]\l\
// \l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15, v10, v44]\l\
// sstore\l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15]\l\
// \l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15, lit47, phi19]\l\
// add\l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15, v48]\l\
// \l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15, v9, v48]\l\
// sstore\l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15]\l\
// \l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15, lit51, phi19]\l\
// add\l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15, v52]\l\
// \l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15, v8, v52]\l\
// sstore\l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15]\l\
// \l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15, lit55, phi19]\l\
// add\l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15, v56]\l\
// \l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15, v7, v56]\l\
// sstore\l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15]\l\
// \l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15, lit59, phi19]\l\
// add\l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15, v60]\l\
// \l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15, v6, v60]\l\
// sstore\l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15]\l\
// \l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15, lit63, phi19]\l\
// add\l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15, v64]\l\
// \l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15, v5, v64]\l\
// sstore\l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15]\l\
// \l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15, lit67, phi19]\l\
// add\l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15, v68]\l\
// \l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15, v4, v68]\l\
// sstore\l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15]\l\
// \l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15, lit71, phi19]\l\
// add\l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15, v72]\l\
// \l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15, v3, v72]\l\
// sstore\l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15]\l\
// \l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15, lit75, phi19]\l\
// add\l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15, v76]\l\
// \l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15, v2, v76]\l\
// sstore\l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15]\l\
// \l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15, lit79, phi19]\l\
// add\l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15, v80]\l\
// \l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15, v1, v80]\l\
// sstore\l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15]\l\
// \l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15, lit83, phi19]\l\
// add\l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15, v84]\l\
// \l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15, v0, v84]\l\
// sstore\l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15]\l\
// \l\
// OUT: [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15]\l\
// "];
// Block1_2 -> Block1_2Exit [arrowhead=none];
// Block1_2Exit [label="Jump" shape=oval];
// Block1_2Exit -> Block1_3 [style="solid"];
// Block1_4 [label="\
// IN: [ReturnLabel[1], JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK, JUNK]\l\
// \l\
// OUT: [ReturnLabel[1]]\l\
// "];
// Block1_4Exit [label="FunctionReturn[]"];
// Block1_4 -> Block1_4Exit;
// Block1_3 [label="\
// IN: [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15]\l\
// \l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15, lit24, phi19]\l\
// add\l\
// [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15, v86]\l\
// \l\
// OUT: [ReturnLabel[1], v16, v15, v14, v13, v12, v11, v10, v9, v8, v7, v6, v5, v4, v3, v2, v1, JUNK, v15, v86]\l\
// "];
// Block1_3 -> Block1_3Exit [arrowhead=none];
// Block1_3Exit [label="Jump" shape=oval];
// Block1_3Exit -> Block1_1 [style="solid"];
// // Spilled[1]: {v0, v19}
// }
