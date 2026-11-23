{
    // Without a memoryguard, functions smaller than 6 are inlined (without constant arguments).
    let x := calldataload(0)
    f(x)
    f(x)
    g(x)
    g(x)

    // Size 5
    function f(a) {
        sstore(add(a, 1), a)
        sstore(a, 5)
    }

    // Size 6
    function g(a) {
        sstore(add(a, 1), a)
        sstore(add(a, 2), a)
    }
}
// ----
// step: fullInliner
//
// {
//     {
//         let x := calldataload(0)
//         let a_9 := x
//         sstore(add(a_9, 1), a_9)
//         sstore(a_9, 5)
//         let a_13 := x
//         sstore(add(a_13, 1), a_13)
//         sstore(a_13, 5)
//         g(x)
//         g(x)
//     }
//     function f(a)
//     {
//         sstore(add(a, 1), a)
//         sstore(a, 5)
//     }
//     function g(a_1)
//     {
//         sstore(add(a_1, 1), a_1)
//         sstore(add(a_1, 2), a_1)
//     }
// }
