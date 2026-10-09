contract C {
    constructor() {
        assembly ("memory-safe") {
            let x0 := calldataload(0)
            let x1 := calldataload(32)
            let x2 := calldataload(64)
            let x3 := calldataload(96)
            let x4 := calldataload(128)
            let x5 := calldataload(160)
            let x6 := calldataload(192)
            let x7 := calldataload(224)
            let x8 := calldataload(256)
            let x9 := calldataload(288)
            let x10 := calldataload(320)
            let x11 := calldataload(352)
            let x12 := calldataload(384)
            let x13 := calldataload(416)
            let x14 := calldataload(448)
            let x15 := calldataload(480)
            let x16 := calldataload(512)
            let x17 := calldataload(544)
            let x18 := calldataload(576)
            let x19 := calldataload(608)
            let x20 := calldataload(640)
            let x21 := calldataload(672)
            let x22 := calldataload(704)
            let x23 := calldataload(736)
            let x24 := calldataload(768)
            let x25 := calldataload(800)
            let x26 := calldataload(832)
            let x27 := calldataload(864)
            let x28 := calldataload(896)
            let x29 := calldataload(928)
            let x30 := calldataload(960)
            let x31 := calldataload(992)
            let x32 := calldataload(1024)
            for {} lt(x0, 5) { x0 := add(x0, 1) } {
                x0 := add(x0, 1)
                x1 := add(x1, 2)
                x2 := add(x2, 3)
                x3 := add(x3, 4)
                x4 := add(x4, 5)
                x5 := add(x5, 6)
                x6 := add(x6, 7)
                x7 := add(x7, 8)
                x8 := add(x8, 9)
                x9 := add(x9, 10)
                x10 := add(x10, 11)
                x11 := add(x11, 12)
                x12 := add(x12, 13)
                x13 := add(x13, 14)
                x14 := add(x14, 15)
                x15 := add(x15, 16)
                x16 := add(x16, 17)
                x17 := add(x17, 18)
                x18 := add(x18, 19)
                x19 := add(x19, 20)
                x20 := add(x20, 21)
                x21 := add(x21, 22)
                x22 := add(x22, 23)
                x23 := add(x23, 24)
                x24 := add(x24, 25)
                x25 := add(x25, 26)
                x26 := add(x26, 27)
                x27 := add(x27, 28)
                x28 := add(x28, 29)
                x29 := add(x29, 30)
                x30 := add(x30, 31)
                x31 := add(x31, 32)
                x32 := add(x32, 33)
            }
            sstore(0, add(x0, x1))
            sstore(1, add(x2, x3))
            sstore(2, add(x4, x5))
            sstore(3, add(x6, x7))
            sstore(4, add(x8, x9))
            sstore(5, add(x10, x11))
            sstore(6, add(x12, x13))
            sstore(7, add(x14, x15))
            sstore(8, add(x16, x17))
            sstore(9, add(x18, x19))
            sstore(10, add(x20, x21))
            sstore(11, add(x22, x23))
            sstore(12, add(x24, x25))
            sstore(13, add(x26, x27))
            sstore(14, add(x28, x29))
            sstore(15, add(x30, x31))
            sstore(16, add(x32, x32))
        }
    }

    function f() public returns (uint) {
        uint val;
        assembly {
            val := sload(16)
        }
        return val;
    }
}
// ====
// compileViaSSACFG: true
// compileViaYul: true
// ----
// f() -> 0xc6
