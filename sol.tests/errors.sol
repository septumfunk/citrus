let cat = unwrap(import("class.sol"));
let meow = unwrap_or(cat.meow, "meeeow!"); // Non existent member
io.println(meow);

let mod = attempt(
    []() { return import("doesnt-exist.sol"); },
    [](err) { io.println(err); return {}; }
);