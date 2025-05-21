{
    let x := calldataload(3)

    switch sload(0)
    case 0 {
        x := calldataload(77)
    }
    case 1 {
        x := calldataload(88)
    }
    default {
        x := calldataload(99)
    }
    sstore(x, 0)
}
// ----
// digraph SSACFG {
// nodesep=0.7;
// graph[fontname="DejaVu Sans"]
// node[shape=box,fontname="DejaVu Sans"];
//
// Entry0 [label="Entry"];
// Entry0 -> Block0_0;
// Block0_0 [label="\
// Block 0; (0, max 5)\nLiveIn: \l\
// LiveOut: v5\l\nv1 := 3\l\
// v2 := calldataload(v1)\l\
// v4 := 0\l\
// v5 := sload(v4)\l\
// v7 := 0\l\
// v6 := eq(v7, v5)\l\
// "];
// Block0_0 -> Block0_0Exit;
// Block0_0Exit [label="{ If v6 | { <0> Zero | <1> NonZero }}" shape=Mrecord];
// Block0_0Exit:0 -> Block0_3 [style="solid"];
// Block0_0Exit:1 -> Block0_2 [style="solid"];
// Block0_2 [label="\
// Block 2; (1, max 2)\nLiveIn: \l\
// LiveOut: v10\l\nv9 := 77\l\
// v10 := calldataload(v9)\l\
// "];
// Block0_2 -> Block0_2Exit [arrowhead=none];
// Block0_2Exit [label="Jump" shape=oval];
// Block0_2Exit -> Block0_1 [style="solid"];
// Block0_3 [label="\
// Block 3; (3, max 5)\nLiveIn: v5\l\
// LiveOut: \l\nv13 := 1\l\
// v11 := eq(v13, v5)\l\
// "];
// Block0_3 -> Block0_3Exit;
// Block0_3Exit [label="{ If v11 | { <0> Zero | <1> NonZero }}" shape=Mrecord];
// Block0_3Exit:0 -> Block0_5 [style="solid"];
// Block0_3Exit:1 -> Block0_4 [style="solid"];
// Block0_1 [label="\
// Block 1; (2, max 2)\nLiveIn: v21\l\
// LiveOut: \l\nv21 := φ(\l\
// 	Block 2 => v10,\l\
// 	Block 4 => v16,\l\
// 	Block 5 => v19\l\
// )\l\
// v20 := 0\l\
// sstore(v20, v21)\l\
// "];
// Block0_1Exit [label="MainExit"];
// Block0_1 -> Block0_1Exit;
// Block0_4 [label="\
// Block 4; (4, max 4)\nLiveIn: \l\
// LiveOut: v16\l\nv15 := 88\l\
// v16 := calldataload(v15)\l\
// "];
// Block0_4 -> Block0_4Exit [arrowhead=none];
// Block0_4Exit [label="Jump" shape=oval];
// Block0_4Exit -> Block0_1 [style="solid"];
// Block0_5 [label="\
// Block 5; (5, max 5)\nLiveIn: \l\
// LiveOut: v19\l\nv18 := 99\l\
// v19 := calldataload(v18)\l\
// "];
// Block0_5 -> Block0_5Exit [arrowhead=none];
// Block0_5Exit [label="Jump" shape=oval];
// Block0_5Exit -> Block0_1 [style="solid"];
// }
