{
    mstore(0x40, memoryguard(0x80))
    let v1 := calldataload(0x01)
    let v2 := calldataload(0x02)
    let v3 := calldataload(0x03)
    let v4 := calldataload(0x04)
    let v5 := calldataload(0x05)
    let v6 := calldataload(0x06)
    let v7 := calldataload(0x07)
    let v8 := calldataload(0x08)
    let v9 := calldataload(0x09)
    let v10 := calldataload(0x0a)
    let v11 := calldataload(0x0b)
    let v12 := calldataload(0x0c)
    let v13 := calldataload(0x0d)
    let v14 := calldataload(0x0e)
    let v15 := calldataload(0x0f)
    let x := calldataload(0x10)
    if calldataload(0x11) { x := 0 }
    // `x` is first read after this operation, so the phi of `x` is scheduled after it in the merge block
    let y := calldataload(0x12)
    // `v1` is out of DUP reach, the shuffler spills the phi of `x`
    sstore(x, v1)
    sstore(v1, add(add(add(v2, v3), add(v4, v5)), add(add(v6, v7), add(v8, v9))))
    sstore(v10, add(add(add(v11, v12), add(v13, v14)), add(v15, y)))
}
// ====
// EVMVersion: >=tangerineWhistle
// stackOptimization: true
// viaSSACFG: true
// ----
// tag_1:
//   mstore(0x40, 0xa0)
//   calldataload(0x01)
//   calldataload(0x02)
//   calldataload(0x03)
//   calldataload(0x04)
//   calldataload(0x05)
//   calldataload(0x06)
//   calldataload(0x07)
//   calldataload(0x08)
//   calldataload(0x09)
//   calldataload(0x0a)
//   calldataload(0x0b)
//   calldataload(0x0c)
//   calldataload(0x0d)
//   calldataload(0x0e)
//   calldataload(0x0f)
//   calldataload(0x10)
//   jumpi(tag_2, calldataload(0x11))
//   jump(tag_4)
// tag_4:
//   jump(tag_3)
// tag_3:
//   dup1
//   0x80
//   mstore
//   calldataload(0x12)
//   swap1
//   pop
//   dup16
//   mload(0x80)
//   sstore
//   swap7
//   swap1
//   swap8
//   add
//   swap8
//   swap1
//   swap9
//   add
//   swap1
//   swap7
//   swap1
//   add
//   swap8
//   swap1
//   swap9
//   add
//   swap9
//   swap1
//   swap10
//   add
//   swap1
//   swap8
//   swap1
//   add
//   swap1
//   swap5
//   swap1
//   add
//   swap1
//   swap8
//   sstore
//   swap2
//   swap1
//   swap6
//   add
//   swap5
//   swap1
//   add
//   swap3
//   add
//   swap1
//   swap2
//   swap1
//   add
//   swap1
//   swap2
//   swap1
//   add
//   swap1
//   sstore
//   stop
// tag_2:
//   pop
//   0x00
//   jump(tag_3)
