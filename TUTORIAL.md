<!-- Generated from docs/lessons.js. Do not edit by hand — run `node tools/gen-tutorial.js`. -->

# Learn Venos

*English | [한국어](TUTORIAL.ko.md)*

A step-by-step introduction to Venos that runs in your browser — nothing to install. Each lesson has an **Open in the playground** link that loads that lesson ready to run.

Variable and function names may be written in Korean (or any language), while keywords like `if` / `for` / `while` / `func` / `class` stay English — so what you learn here carries straight over to Python or C.

## Contents

1. [Printing](#1-printing)
2. [Variables](#2-variables)
3. [Input and string interpolation](#3-input-and-string-interpolation)
4. [Conditions](#4-conditions)
5. [Repeating with for](#5-repeating-with-for)
6. [Repeating with while](#6-repeating-with-while)
7. [Lists](#7-lists)
8. [Dictionaries](#8-dictionaries)
9. [Functions](#9-functions)
10. [Classes](#10-classes)
11. [Handling errors](#11-handling-errors)
12. [Mini project: number guessing](#12-mini-project-number-guessing)

---

## 1. Printing

`print` shows a value on screen. Separate several values with commas to put them on one line. Anything after `#` is a comment and is not run.

Press ▶ Run, then change the text inside the quotes and run it again.

```
# first Venos program
print "Hello!"
print "Hi :)"

# print multiple values with commas
print "1 + 2 =", 1 + 2
```

▶ **[Open in the playground](https://vpdrla.github.io/Venos/#lesson=print)**

---

## 2. Variables

Create a variable with `let`. **Names can be written in Korean** (or any language).

Once created, assign to it without `let`, and use `+=` to add to it.

`이름` means "name" and `나이` means "age" — the identifiers stay Korean here on purpose. Try changing `나이` and running it again.

```
let 이름 = "미르"
let 나이 = 15

print 이름
print 나이

나이 += 1          # age + 1
print "next year:", 나이
```

▶ **[Open in the playground](https://vpdrla.github.io/Venos/#lesson=variables)**

---

## 3. Input and string interpolation

`input` asks the user for a value — in the playground a small dialog appears.

Inside a string, `{ }` inserts the value in it. That is **string interpolation**, and it reads better than joining with `+`.

Run it and type any name into the dialog.

```
let name = input "What is your name? "
let age = 15

print "Hello, {name}!"
print "{name} becomes {age + 1} next year."
```

▶ **[Open in the playground](https://vpdrla.github.io/Venos/#lesson=input)**

---

## 4. Conditions

`if` runs the block only when the condition is true; otherwise it falls through to `else if`, then `else`.

`then` is optional — it is there only to make the line read like a sentence.

Change `score` and see how the grade changes.

```
let score = 85

if score >= 90 then {
    print "grade A"
} else if score >= 80 {
    print "grade B"
} else {
    print "Better next time!"
}

print "The score is {score}"
```

▶ **[Open in the playground](https://vpdrla.github.io/Venos/#lesson=if)**

---

## 5. Repeating with for

`for i = 1 to 5` repeats from 1 to 5, **including both ends**.

Use `step` to skip, and a negative step to count down.

`"*" * i` is a string of i stars, so a loop can draw with it.

Change the numbers to print a different multiplication table.

```
# 3 times table
for i = 1 to 9 {
    print "3 x {i} = {3 * i}"
}

print ""
# backwards
for i = 5 to 1 step -1 {
    print i
}
print "Liftoff!"

print ""
# a staircase of stars
for i = 1 to 5 { print "*" * i }
```

▶ **[Open in the playground](https://vpdrla.github.io/Venos/#lesson=for)**

---

## 6. Repeating with while

`while` keeps repeating **as long as** the condition is true — use it when you do not know the count in advance.

`break` leaves the loop immediately. Make sure the condition eventually becomes false, or the loop never ends.

Change `moneyleft` and see how many you can buy.

```
let moneyleft = 50
let price = 15
let count = 0

while moneyleft >= price do {
    moneyleft -= price
    count += 1
}

print "Bought {count}, with {moneyleft} dollars left."
```

▶ **[Open in the playground](https://vpdrla.github.io/Venos/#lesson=while)**

---

## 7. Lists

A list holds values in order. **Indices start at 1**, not 0!

`push` appends, `len` counts, and `for ... in` walks through the items.

Try adding more fruit to the list.

```
let fruit = ["apple", "banana", "grape"]

print "first:", fruit[1]      # counting starts at 1!
print "count:", len(fruit)

push(fruit, "strawberry")

for one in fruit {
    print "-", one
}
```

▶ **[Open in the playground](https://vpdrla.github.io/Venos/#lesson=lists)**

---

## 8. Dictionaries

A dictionary looks values up by a **key**, and keys are always strings.

Reading a missing key is an error, so get into the habit of checking with `has` first.

Try adding more subjects and scores.

```
let scores = {"math": 90, "english": 85}

scores["science"] = 95        # assigning a new key creates it
scores["math"] += 5

for subject in scores {
    print "{subject} -> {scores[subject]}"
}

if has(scores, "music") {
    print "music:", scores["music"]
} else {
    print "No music score yet."
}
```

▶ **[Open in the playground](https://vpdrla.github.io/Venos/#lesson=dicts)**

---

## 9. Functions

Give a piece of behavior a name with `func` and reuse it as often as you like; `return` hands a result back.

A function can even **call itself** (recursion) — `factorial` below does exactly that.

Try adding one more `greet("...")` line.

```
func greet(name) {
    print "Hello, {name}!"
}

func factorial(n) {
    if n <= 1 then { return 1 }
    return n * factorial(n - 1)
}

greet("Mir")
greet("Sky")
print "5! =", factorial(5)
```

▶ **[Open in the playground](https://vpdrla.github.io/Venos/#lesson=functions)**

---

## 10. Classes

A class bundles **data and behavior** together. `init` is the constructor, called automatically when you create one.

`self` means "this object" — use `self.name` to read and write its own fields.

Try creating one more dog.

```
class Dog {
    func init(name) {
        self.name = name
        self.age = 0
    }
    func bark() { print "{self.name}: woof!" }
    func birthday() {
        self.age += 1
        print "{self.name} is now {self.age}"
    }
}

let buddy = Dog("Buddy")
buddy.bark()
buddy.birthday()
```

▶ **[Open in the playground](https://vpdrla.github.io/Venos/#lesson=classes)**

---

## 11. Handling errors

An error stops the program. Inside `try`, an error jumps to `catch` instead of stopping everything.

The message lands in the variable you name after `catch`, as a string. You can raise your own with `error(...)`.

Try changing the divisor to something other than 0.

```
let divisor = 0

try {
    print 10 / divisor
    print "this line never runs"
} catch err {
    print "Something went wrong:", err
}

print "The program keeps going."
```

▶ **[Open in the playground](https://vpdrla.github.io/Venos/#lesson=errors)**

---

## 12. Mini project: number guessing

This uses everything so far — variables, `while`, `if`, `input`, functions, and interpolation.

`random(1, 50)` picks any number from 1 to 50.

Widen the range, add a limit on tries, or make the hints friendlier. When it works, send it to a friend with the **Share** button!

```
let answer = random(1, 50)
let tries = 0

print "Guess a number between 1 and 50!"

while true {
    let guess = input "number: "
    tries += 1

    if guess == answer {
        print "Correct! You got it in {tries} tries"
        break
    } else if guess < answer { print "Higher" }
    else { print "Lower" }
}
```

▶ **[Open in the playground](https://vpdrla.github.io/Venos/#lesson=project)**

---

## Where to go next

- The full syntax lives in the [language spec](VENOS_SPEC.en.md).
- A bigger example: [`examples/rpg.en.my`](examples/rpg.en.my) — a 227-line text RPG built from what you just learned.
- Use the **🔗 Share** button in the playground to turn your program into a link you can send to a friend or teacher.
- **When you are ready for the next language**: the **🐍 Python** button (or `venos topython program.my`) rewrites what you just wrote as Python, keeping your own variable and function names.
