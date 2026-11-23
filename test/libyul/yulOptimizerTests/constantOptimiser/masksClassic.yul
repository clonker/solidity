{
  // Without the SSA CFG code transform, masks are not computed by shifting not(0).
  let lowMask := 0xffffffffffffffffffffffffffffffffffffffff
  let lowMask255 := 0x7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff
  let highMask := 0xffffffffffffffffffffffff0000000000000000000000000000000000000000
  let middleMask := 0x0000000000000000ffffffffffffffffffffffffffffffff0000000000000000
  let allOnes := 0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff
  let singleBit := 0x0000000000000100000000000000000000000000000000000000000000000000
}
// ====
// EVMVersion: >=constantinople
// ----
// step: constantOptimiser
//
// {
//     let lowMask := sub(shl(160, 1), 1)
//     let lowMask255 := sub(shl(255, 1), 1)
//     let highMask := shl(160, 0xffffffffffffffffffffffff)
//     let middleMask := 0x0000000000000000ffffffffffffffffffffffffffffffff0000000000000000
//     let allOnes := not(0)
//     let singleBit := shl(200, 1)
// }
