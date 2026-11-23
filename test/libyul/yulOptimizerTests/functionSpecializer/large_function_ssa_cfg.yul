{
    // For the SSA CFG code transform, functions larger than 32 are not specialized.
    fits(1, calldataload(0))
    tooLarge(1, calldataload(0))

    // Size 32
    function fits(a, b) {
        sstore(add(a, 1), add(b, 1))
        sstore(add(a, 2), add(b, 2))
        sstore(add(a, 3), add(b, 3))
        sstore(add(a, 4), add(b, 4))
        sstore(add(a, 5), add(b, 5))
        sstore(add(a, 6), add(b, 6))
        sstore(a, 7)
    }

    // Size 33
    function tooLarge(a, b) {
        sstore(add(a, 1), add(b, 1))
        sstore(add(a, 2), add(b, 2))
        sstore(add(a, 3), add(b, 3))
        sstore(add(a, 4), add(b, 4))
        sstore(add(a, 5), add(b, 5))
        sstore(add(a, 6), add(b, 6))
        sstore(a, 7)
        sstore(b, a)
    }
}
// ====
// viaSSACFG: true
// ----
// step: functionSpecializer
//
// {
//     fits_1(calldataload(0))
//     tooLarge(1, calldataload(0))
//     function fits_1(b_3)
//     {
//         let a_4 := 1
//         sstore(add(a_4, 1), add(b_3, 1))
//         sstore(add(a_4, 2), add(b_3, 2))
//         sstore(add(a_4, 3), add(b_3, 3))
//         sstore(add(a_4, 4), add(b_3, 4))
//         sstore(add(a_4, 5), add(b_3, 5))
//         sstore(add(a_4, 6), add(b_3, 6))
//         sstore(a_4, 7)
//     }
//     function fits(a, b)
//     {
//         sstore(add(a, 1), add(b, 1))
//         sstore(add(a, 2), add(b, 2))
//         sstore(add(a, 3), add(b, 3))
//         sstore(add(a, 4), add(b, 4))
//         sstore(add(a, 5), add(b, 5))
//         sstore(add(a, 6), add(b, 6))
//         sstore(a, 7)
//     }
//     function tooLarge(a_1, b_2)
//     {
//         sstore(add(a_1, 1), add(b_2, 1))
//         sstore(add(a_1, 2), add(b_2, 2))
//         sstore(add(a_1, 3), add(b_2, 3))
//         sstore(add(a_1, 4), add(b_2, 4))
//         sstore(add(a_1, 5), add(b_2, 5))
//         sstore(add(a_1, 6), add(b_2, 6))
//         sstore(a_1, 7)
//         sstore(b_2, a_1)
//     }
// }
