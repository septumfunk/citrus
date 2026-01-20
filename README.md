# solus
  solus is an embeddable scripting language with a register based VM and a focus on doing everything exactly the way *I* want to. The design of the compiler and VM are both heavily inspired by Lua's, alongside numerous other language semantics. Solus aims to reduce the amount of code required for me to bind my projects' C functions and structs with a high-level dynamically typed scripting language for both rapid prototyping and ease of use for the end user.

# Examples
## Class
```
let cat = {
    name = "Jessica"
    fav_food = "Meow Mix"
    color = "Brown"
    babies = 0

    give_birth = [cat]() {
        cat.babies += 1;
        io.println("A kitten is born! That's " + string(cat.babies) + " babies!");
    }

    describe = [cat]() {
        io.println("My name is " + cat.name + ", my fav food is " + cat.fav_food +
            ", and my color is " + cat.color + "!");
        io.println("I've had " + string(cat.babies) + " babies so far.");
    }
};

let x = 0;
while x < 3 {
    cat.give_birth();
    x += 1;
}
cat.describe();
return cat;
```
**Output**
```
A kitten is born! That's 1 babies!
A kitten is born! That's 2 babies!
A kitten is born! That's 3 babies!
My name is Jessica, my fav food is Meow Mix, and my color is Brown!
I've had 3 babies so far.
Returned: (obj) 0xca90080a8
```
## Errors
```
let cat = unwrap(import("class.sol"));
let meow = unwrap_or(cat.meow, "meeeow!"); // Non existent member
io.println(meow);

let mod = attempt(
    []() { return import("doesnt-exist.sol"); },
    [](err) { io.println(err); return {}; }
);
```
