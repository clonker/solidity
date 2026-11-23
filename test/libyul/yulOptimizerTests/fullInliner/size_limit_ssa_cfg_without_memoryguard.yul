{
    // For the SSA CFG code transform, functions smaller than 14 are inlined (without constant
    // arguments), with and without a memoryguard.
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
//         let x := calldataload(0)
//         let a_19 := x
//         sstore(add(a_19, 1), a_19)
//         sstore(add(a_19, 2), a_19)
//         sstore(add(a_19, 3), a_19)
//         sstore(add(a_19, 4), a_19)
//         sstore(a_19, a_19)
//         let a_28 := x
//         sstore(add(a_28, 1), a_28)
//         sstore(add(a_28, 2), a_28)
//         sstore(add(a_28, 3), a_28)
//         sstore(add(a_28, 4), a_28)
//         sstore(a_28, a_28)
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
