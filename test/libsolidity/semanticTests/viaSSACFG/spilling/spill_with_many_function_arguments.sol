contract C {
    constructor(uint v1, uint v2, uint v3, uint v4, uint v5, uint v6, uint v7, uint v8, uint v9, uint v10, uint v11, uint v12, uint v13, uint v14, uint v15, uint v16, uint v17, uint v18, uint v19, uint v20, uint v21, uint v22, uint v23, uint v24, uint v25, uint v26, uint v27, uint v28, uint v29, uint v30, uint v31, uint v32, uint v33, uint v34) {
        assembly ("memory-safe") {
            f(
                v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14, v15, v16, v17, v18, v19, v20, v21, v22, v23, v24, v25, v26, v27, v28, v29, v30, v31, v32, v33, v34
            )
            function f(b1, b2, b3, b4, b5, b6, b7, b8, b9, b10, b11, b12, b13, b14, b15, b16, b17, b18, b19, b20, b21, b22, b23, b24, b25, b26, b27, b28, b29, b30, b31, b32, b33, b34) {
                sstore(0, b34)
                sstore(1, b33)
                sstore(2, b32)
                sstore(3, b31)
                sstore(4, b30)
                sstore(5, b29)
                sstore(6, b28)
                sstore(7, b27)
                sstore(8, b26)
                sstore(9, b25)
                sstore(10, b24)
                sstore(11, b23)
                sstore(12, b22)
                sstore(13, b21)
                sstore(14, b20)
                sstore(15, b19)
                sstore(16, b18)
                sstore(17, b17)
                sstore(18, b16)
                sstore(19, b15)
                sstore(20, b14)
                sstore(21, b13)
                sstore(22, b12)
                sstore(23, b11)
                sstore(24, b10)
                sstore(25, b9)
                sstore(26, b8)
                sstore(27, b7)
                sstore(28, b6)
                sstore(29, b5)
                sstore(30, b4)
                sstore(31, b3)
                sstore(32, b2)
                sstore(33, b1)
            }
        }
    }

    function f() public returns(uint) {
        uint val;
        assembly { val := sload(33) }
        return val;
    }

    function g(uint slot) public returns(uint) {
        uint val;
        assembly { val := sload(slot) }
        return val;
    }
}
// ====
// requiresYulOptimizer: full
// compileViaSSACFG: true
// compileViaYul: true
// ----
// constructor(): 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34 ->
// gas irOptimized: 818650
// gas irOptimized code: 20800
// f() -> 1
// g(uint256): 0 -> 34
// g(uint256): 1 -> 33
// g(uint256): 15 -> 19
// g(uint256): 31 -> 3
