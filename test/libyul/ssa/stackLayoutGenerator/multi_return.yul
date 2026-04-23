{
    function pair(a) -> x, y {
        x := add(a, 1)
        y := add(a, 2)
    }
    let p, q := pair(42)
    let r, s := pair(p)
    sstore(q, add(r, s))
}
// ----
// digraph SSACFG {
// nodesep=0.7;
// graph[fontname="DejaVu Sans", rankdir=LR]
// node[shape=box,fontname="DejaVu Sans"];
//
// Entry [label="Entry"];
// Entry -> Block0_0;
// Block0_0 [label="\
// IN: []\l\
// \l\
// [FunctionCallReturnLabel[0], lit0]\l\
// pair\l\
// [FunctionCallReturnLabel[0], v1, v1.1]\l\
// \l\
// [v1, v1.1, FunctionCallReturnLabel[1], v1]\l\
// pair\l\
// [v1, v1.1, FunctionCallReturnLabel[1], v2, v2.1]\l\
// \l\
// [JUNK, v1.1, v2.1, v2]\l\
// add\l\
// [JUNK, v1.1, v3]\l\
// \l\
// [JUNK, v3, v1.1]\l\
// sstore\l\
// [JUNK]\l\
// \l\
// OUT: [JUNK]\l\
// "];
// Block0_0Exit [label="MainExit"];
// Block0_0 -> Block0_0Exit;
// FunctionEntry_pair_0 [label="function pair:
//  [2 returns] := pair(v0)"];
// FunctionEntry_pair_0 -> Block1_0;
// Block1_0 [label="\
// IN: [ReturnLabel[1], v0]\l\
// \l\
// [ReturnLabel[1], v0, lit2, v0]\l\
// add\l\
// [ReturnLabel[1], v0, v3]\l\
// \l\
// [ReturnLabel[1], v3, lit4, v0]\l\
// add\l\
// [ReturnLabel[1], v3, v5]\l\
// \l\
// OUT: [v3, v5, ReturnLabel[1]]\l\
// "];
// Block1_0Exit [label="FunctionReturn[v3, v5]"];
// Block1_0 -> Block1_0Exit;
// }
