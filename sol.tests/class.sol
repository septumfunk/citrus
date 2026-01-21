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
while x < 3: {
    cat.give_birth();
    x += 1;
}
cat.describe();

return cat;