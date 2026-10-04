{
    function f(a, b, c, d, e, ff) -> ret1, ret2, ret3, ret4, ret5, ret6 {
        ret1 := ff
        ret2 := e
        ret3 := d
        ret4 := c
        ret5 := b
        ret6 := a
    }
    let a,b,c,d,e,ff := f(0, 1, 2, 3, 4, 5)
    mstore(33, a)
    mstore(33, b)
    mstore(33, c)
    mstore(33, d)
    mstore(33, e)
    mstore(33, ff)
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
// [FunctionCallReturnLabel[0], lit0, lit1, lit2, lit3, lit4, lit5]\l\
// f\l\
// [v7, v8, v9, v10, v11, v12]\l\
// \l\
// [v12, v8, v9, v10, v11, v7, lit13]\l\
// mstore\l\
// [v12, v8, v9, v10, v11]\l\
// \l\
// [v12, v11, v9, v10, v8, lit13]\l\
// mstore\l\
// [v12, v11, v9, v10]\l\
// \l\
// [v12, v11, v10, v9, lit13]\l\
// mstore\l\
// [v12, v11, v10]\l\
// \l\
// [v12, v11, v10, lit13]\l\
// mstore\l\
// [v12, v11]\l\
// \l\
// [v12, v11, lit13]\l\
// mstore\l\
// [v12]\l\
// \l\
// [v12, lit13]\l\
// mstore\l\
// []\l\
// \l\
// OUT: []\l\
// "];
// Block0_0Exit [label="MainExit"];
// Block0_0 -> Block0_0Exit;
// FunctionEntry_f_0 [label="function f:
//  [6 returns] := f(v0, v1, v2, v3, v4, v5)
// spilled: {}"];
// FunctionEntry_f_0 -> Block1_0;
// Block1_0 [label="\
// IN: [ReturnLabel[1], v5, v4, v3, v2, v1, v0]\l\
// \l\
// OUT: [v5, v4, v3, v2, v1, v0, ReturnLabel[1]]\l\
// "];
// Block1_0Exit [label="FunctionReturn[v5, v4, v3, v2, v1, v0]"];
// Block1_0 -> Block1_0Exit;
// }
