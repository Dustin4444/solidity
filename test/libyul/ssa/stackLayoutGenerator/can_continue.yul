{
    // a function that can continue depending on condition
    function revert_wrapper(val, condition)
    {
        if iszero(condition)
        {
            revert(val, val)
        }
        // if we don't revert, we return nothing and the stack out should contain nothing but the return label
    }

    revert_wrapper(42, 1)
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
// OUT: []\l\
// "];
// Block0_0Exit [label="MainExit"];
// Block0_0 -> Block0_0Exit;
// FunctionEntry_allocate_unbounded_0 [label="function allocate_unbounded:
//  memPtr := allocate_unbounded()"];
// FunctionEntry_allocate_unbounded_0 -> Block1_0;
// Block1_0 [label="\
// IN: [ReturnLabel[1]]\l\
// \l\
// [ReturnLabel[1], lit1]\l\
// mload\l\
// [ReturnLabel[1], v0]\l\
// \l\
// OUT: [v0, ReturnLabel[1]]\l\
// "];
// Block1_0Exit [label="FunctionReturn[v0]"];
// Block1_0 -> Block1_0Exit;
// FunctionEntry_abi_encode_string_0 [label="function abi_encode_string:
//  tail := abi_encode_string(v0, v1)"];
// FunctionEntry_abi_encode_string_0 -> Block2_0;
// Block2_0 [label="\
// IN: [ReturnLabel[2], v1, v0]\l\
// \l\
// [ReturnLabel[2], JUNK, v0, lit1, v0]\l\
// add\l\
// [ReturnLabel[2], JUNK, v0, v2]\l\
// \l\
// [ReturnLabel[2], JUNK, v0, v2, v0, v2]\l\
// sub\l\
// [ReturnLabel[2], JUNK, v0, v2, v3]\l\
// \l\
// [ReturnLabel[2], JUNK, v3, v2, lit0, v0]\l\
// add\l\
// [ReturnLabel[2], JUNK, v3, v2, v4]\l\
// \l\
// [ReturnLabel[2], JUNK, v2, v3, v4]\l\
// mstore\l\
// [ReturnLabel[2], JUNK, v2]\l\
// \l\
// OUT: [v2, ReturnLabel[2]]\l\
// "];
// Block2_0Exit [label="FunctionReturn[v2]"];
// Block2_0 -> Block2_0Exit;
// FunctionEntry_require_helper_string_0 [label="function require_helper_string:
//  require_helper_string(v0, v1)"];
// FunctionEntry_require_helper_string_0 -> Block3_0;
// Block3_0 [label="\
// IN: [ReturnLabel[3], v1, v0]\l\
// \l\
// [ReturnLabel[3], v1, v0]\l\
// iszero\l\
// [ReturnLabel[3], v1, v2]\l\
// \l\
// OUT: [ReturnLabel[3], v1, v2]\l\
// "];
// Block3_0 -> Block3_0Exit;
// Block3_0Exit [label="{ If v2 | { <0> Zero | <1> NonZero }}" shape=Mrecord];
// Block3_0Exit:0 -> Block3_2 [style="solid"];
// Block3_0Exit:1 -> Block3_1 [style="solid"];
// Block3_1 [label="\
// IN: [ReturnLabel[3], v1]\l\
// \l\
// [ReturnLabel[3], v1, FunctionCallReturnLabel[0]]\l\
// allocate_unbounded\l\
// [ReturnLabel[3], v1, FunctionCallReturnLabel[0], v3]\l\
// \l\
// [ReturnLabel[3], v1, v3, lit1, lit2]\l\
// shl\l\
// [ReturnLabel[3], v1, v3, v4]\l\
// \l\
// [ReturnLabel[3], v1, v3, v4, v3]\l\
// mstore\l\
// [ReturnLabel[3], v1, v3]\l\
// \l\
// [ReturnLabel[3], v1, v3, lit3, v3]\l\
// add\l\
// [ReturnLabel[3], v1, v3, v5]\l\
// \l\
// [ReturnLabel[3], v1, v3, v5, FunctionCallReturnLabel[1], v1, v5]\l\
// abi_encode_string\l\
// [ReturnLabel[3], v1, v3, v5, FunctionCallReturnLabel[1], v6]\l\
// \l\
// [ReturnLabel[3], JUNK, v3, JUNK, v6, v3, v6]\l\
// sub\l\
// [ReturnLabel[3], JUNK, v3, JUNK, v6, v7]\l\
// \l\
// [ReturnLabel[3], JUNK, v3, JUNK, JUNK, v7, v3]\l\
// revert\l\
// [ReturnLabel[3], JUNK, v3, JUNK, JUNK]\l\
// \l\
// OUT: [ReturnLabel[3], JUNK, v3, JUNK, JUNK]\l\
// "];
// Block3_1Exit [label="Terminated"];
// Block3_1 -> Block3_1Exit;
// Block3_2 [label="\
// IN: [ReturnLabel[3], JUNK]\l\
// \l\
// OUT: [ReturnLabel[3]]\l\
// "];
// Block3_2Exit [label="FunctionReturn[]"];
// Block3_2 -> Block3_2Exit;
// }
