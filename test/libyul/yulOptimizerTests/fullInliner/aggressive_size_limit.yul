{
    // With a memoryguard, functions smaller than 8 are inlined (without constant arguments).
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
// ----
// step: fullInliner
//
// {
//     {
//         mstore(0x40, memoryguard(0x80))
//         let x := calldataload(0)
//         f(x)
//         f(x)
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
