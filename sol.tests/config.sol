let default = {
    repeat = 100
    min_i = -256
    max_i = 256
};

let cfg = unwrap_or(do {
    let f = io.fread("test.cfg");
    if type(f) == "err": f
    else: eval(f)
}, do {
    unwrap(io.fwrite("test.cfg", obj.stringify(default)));
    default
});

let x = 0;
while x < cfg.repeat: {
    io.println(math.randi(cfg.min_i, cfg.max_i));
    x += 1;
}