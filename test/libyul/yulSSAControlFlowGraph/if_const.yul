{
    let x := calldataload(3)
    // this should not appear in the output at all
    if 0 {
        x := calldataload(77)
    }
    // this should avoid a conditional jump
    if 33 {
        x := calldataload(42)
    }
    let y := calldataload(x)
    sstore(y, 0)
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
// Block 0; (0, max 0)\nLiveIn: \l\
// LiveOut: \l\nv1 := 3\l\
// v2 := calldataload(v1)\l\
// v4 := 42\l\
// v5 := calldataload(v4)\l\
// v6 := calldataload(v5)\l\
// v8 := 0\l\
// sstore(v8, v6)\l\
// "];
// Block0_0Exit [label="MainExit"];
// Block0_0 -> Block0_0Exit;
// }
