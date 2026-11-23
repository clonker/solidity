{
    // For the SSA CFG code transform, functions smaller than 14 are inlined (without constant
    // arguments), with and without a memoryguard.
    mstore(0x40, memoryguard(0x80))
    let x := calldataload(0)
    f(x)
    f(x)
    g(x)
    g(x)

    // Size 13
    function f(a) {
        sstore(add(a, 1), a)
        sstore(add(a, 2), a)
        sstore(add(a, 3), a)
        sstore(add(a, 4), a)
        sstore(a, a)
    }

    // Size 14
    function g(a) {
        sstore(add(a, 1), a)
        sstore(add(a, 2), a)
        sstore(add(a, 3), a)
        sstore(add(a, 4), a)
        sstore(a, 5)
    }
}
// ====
// viaSSACFG: true
// ----
// step: fullInliner
//
// {
//     {
//         mstore(0x40, memoryguard(0x80))
//         let x := calldataload(0)
//         let a_21 := x
//         sstore(add(a_21, 1), a_21)
//         sstore(add(a_21, 2), a_21)
//         sstore(add(a_21, 3), a_21)
//         sstore(add(a_21, 4), a_21)
//         sstore(a_21, a_21)
//         let a_30 := x
//         sstore(add(a_30, 1), a_30)
//         sstore(add(a_30, 2), a_30)
//         sstore(add(a_30, 3), a_30)
//         sstore(add(a_30, 4), a_30)
//         sstore(a_30, a_30)
//         g(x)
//         g(x)
//     }
//     function f(a)
//     {
//         sstore(add(a, 1), a)
//         sstore(add(a, 2), a)
//         sstore(add(a, 3), a)
//         sstore(add(a, 4), a)
//         sstore(a, a)
//     }
//     function g(a_1)
//     {
//         sstore(add(a_1, 1), a_1)
//         sstore(add(a_1, 2), a_1)
//         sstore(add(a_1, 3), a_1)
//         sstore(add(a_1, 4), a_1)
//         sstore(a_1, 5)
//     }
// }
