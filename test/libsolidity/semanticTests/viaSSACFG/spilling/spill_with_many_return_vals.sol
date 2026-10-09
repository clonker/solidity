contract C {
    constructor() {
        assembly ("memory-safe") {
            let v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14, v15, v16, v17 := many()
            sstore(v0, v1)
            sstore(v2, v3)
            sstore(v4, v5)
            sstore(v6, v7)
            sstore(v8, v9)
            sstore(v10, v11)
            sstore(v12, v13)
            sstore(v14, v15)
            sstore(v16, v17)

            function many() -> r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11, r12, r13, r14, r15, r16, r17
            {
                r0 := 1
                r1 := 2
                r2 := 3
                r3 := 4
                r4 := 5
                r5 := 6
                r6 := 7
                r7 := 8
                r8 := 9
                r9 := 10
                r10 := 11
                r11 := 12
                r12 := 13
                r13 := 14
                r14 := 15
                r15 := 16
                r16 := 17
                r17 := 18
            }
        }
    }

    function f() public returns (uint) {
        uint val;
        assembly { val := sload(17) }
        return val;
    }

}

// ====
// requiresYulOptimizer: full
// compileViaSSACFG: true
// compileViaYul: true
// ----
// f() -> 0x12
