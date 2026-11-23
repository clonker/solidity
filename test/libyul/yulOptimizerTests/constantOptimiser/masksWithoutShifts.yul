{
  let lowMask := 0xffffffffffffffffffffffffffffffffffffffff
  let highMask := 0xffffffffffffffffffffffff0000000000000000000000000000000000000000
}
// ====
// EVMVersion: <constantinople
// viaSSACFG: true
// ----
// step: constantOptimiser
//
// {
//     let lowMask := 0xffffffffffffffffffffffffffffffffffffffff
//     let highMask := not(0xffffffffffffffffffffffffffffffffffffffff)
// }
