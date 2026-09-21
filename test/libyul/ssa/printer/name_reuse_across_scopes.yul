// The SSA CFG builder resolves identifiers through the Scope objects of AsmAnalysisInfo, so it does
// not require a disambiguated AST. Yul forbids shadowing, hence the only name reuse that can reach
// the builder is between disjoint scopes: two sibling blocks may each declare `x` and define `f`.
// These must stay distinct values/functions here.
{
    {
        function f(a) -> r { r := add(a, 1) }
        let x := 1
        sstore(0, f(x))
    }
    {
        function f(a) -> r { r := mul(a, 2) }
        let x := 3
        sstore(1, f(x))
    }
}
// ----
// #0:
//     v0 = const 0x01
//     v1 = call @f#1 v0
//     v2 = const 0x00
//     builtin @sstore v2, v1
//     v4 = const 0x03
//     v5 = call @f#2 v4
//     builtin @sstore v0, v5
//     main_exit
//
// func @f#1(args: (v0)) -> 1 {
// #0:
//     v0 = arg 0
//     v1 = const 0x00
//     v2 = const 0x01
//     v3 = builtin @add v0, v2
//     return v3
// }
//
// func @f#2(args: (v0)) -> 1 {
// #0:
//     v0 = arg 0
//     v1 = const 0x00
//     v2 = const 0x02
//     v3 = builtin @mul v0, v2
//     return v3
// }
//
