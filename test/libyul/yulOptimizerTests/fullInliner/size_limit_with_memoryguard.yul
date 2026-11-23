{
    // With a memoryguard, functions smaller than 8 are inlined (without constant arguments).
    mstore(0x40, memoryguard(0x80))
    let x := calldataload(0)
    f(x)
    f(x)
    g(x)
    g(x)

    // Size 7
    function f(a) {
        sstore(add(a, 1), a)
        sstore(add(a, 2), a)
        sstore(a, a)
    }

    // Size 8
    function g(a) {
        sstore(add(a, 1), a)
        sstore(add(a, 2), a)
        sstore(a, 5)
    }
}
// ----
// step: fullInliner
//
// {
//     {
//         mstore(0x40, memoryguard(0x80))
//         let x := calldataload(0)
//         let a_13 := x
//         sstore(add(a_13, 1), a_13)
//         sstore(add(a_13, 2), a_13)
//         sstore(a_13, a_13)
//         let a_18 := x
//         sstore(add(a_18, 1), a_18)
//         sstore(add(a_18, 2), a_18)
//         sstore(a_18, a_18)
//         g(x)
//         g(x)
//     }
//     function f(a)
//     {
//         sstore(add(a, 1), a)
//         sstore(add(a, 2), a)
//         sstore(a, a)
//     }
//     function g(a_1)
//     {
//         sstore(add(a_1, 1), a_1)
//         sstore(add(a_1, 2), a_1)
//         sstore(a_1, 5)
//     }
// }
