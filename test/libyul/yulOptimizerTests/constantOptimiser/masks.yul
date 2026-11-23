{
  let lowMask := 0xffffffffffffffffffffffffffffffffffffffff
  let lowMask255 := 0x7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff
  let highMask := 0xffffffffffffffffffffffff0000000000000000000000000000000000000000
  let middleMask := 0x0000000000000000ffffffffffffffffffffffffffffffff0000000000000000
  let allOnes := 0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff
  let singleBit := 0x0000000000000100000000000000000000000000000000000000000000000000
}
// ====
// EVMVersion: >=constantinople
// viaSSACFG: true
// ----
// step: constantOptimiser
//
// {
//     let lowMask := shr(96, not(0))
//     let lowMask255 := shr(1, not(0))
//     let highMask := shl(160, not(0))
//     let middleMask := shl(64, shr(128, not(0)))
//     let allOnes := not(0)
//     let singleBit := shl(200, 1)
// }
