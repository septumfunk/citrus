let asm_fun = asm(2) [](x) {
    GUPO 1 0 "io";
    LOAD 2 "println";
    GET  1 1 2;
    CALL 2 1 0;
    RET  2;
};

let i = 0;
while i < 10 {
    asm_fun(string(i) + " Wowee!");
    i = i + 1;
}