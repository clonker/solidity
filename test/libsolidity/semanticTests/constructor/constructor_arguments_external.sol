contract Main {
    bytes3 name;
    bool flag;

    constructor(bytes3 x, bool f) {
        name = x;
        flag = f;
    }

    function getName() public returns (bytes3 ret) {
        return name;
    }

    function getFlag() public returns (bool ret) {
        return flag;
    }
}
// ----
// constructor(): "abc", true
// gas irOptimized: 80158
// gas irOptimized code: 24000
// gas legacy: 85098
// gas legacy code: 58200
// gas legacyOptimized: 80132
// gas legacyOptimized code: 22800
// gas ssaCFGOptimized: 79864
// gas ssaCFGOptimized code: 21800
// getFlag() -> true
// getName() -> "abc"
