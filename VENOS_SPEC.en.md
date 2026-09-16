# Venos Language Specification (v0.6.0)

*English | [한국어](VENOS_SPEC.md)*

Hand this document to an AI assistant and it can write valid Venos code for you.
Source files use the `.my` extension, encoded in UTF-8.

## Running programs
```
venos program.my           # run directly (interpreter)
venos build program.my     # transpile to C++, compile with g++ → native executable
venos build program.my run # build, then run immediately
venos topython program.my  # write the same program as Python (.my → .py)
venos                      # interactive shell (create/choose/code/run/build/...)
venos --help               # usage; venos --version prints the version
```
Inside the shell, `repl` starts a line-by-line REPL — type a bare expression to see its value.

Exit status is 0 on success and 1 on failure (an uncaught error, input that ran out, a program
`topython` refused, a missing file, an unknown argument), so it drops straight into a grading
script or a Makefile.

## Types
Numbers (no integer/float distinction), strings, lists, dictionaries, objects (class instances).
Numbers are a single real type, so **integers are exact up to about 9 quadrillion (2^53)** — `factorial(20)` is exact, `factorial(25)` is an approximation. When you need bigger, `topython` is the answer: Python's integers have no limit.
`true`/`false` are the numbers 1/0. Truthiness: 0, the empty string, and empty lists/dictionaries are falsy. Objects are always truthy.
Lists, dictionaries, and objects use **reference semantics** — copying a variable or passing an argument shares the same underlying value (like Python).
Strings are **immutable** — `s[1] = "x"` is not allowed; build a new string with `replace()` and reassign.

## Variables and assignment
```
let x = 10          # declaration (omitting the initializer gives 0)
let name = "Mir"    # identifiers may use any language (full UTF-8)
x = 20              # assignment (must be declared with let first)
x += 1              # += -= *= /= supported (also on list elements and object fields)
```

## Output / input
```
print "hello"                     # newline appended automatically
print "x =", x, "end"             # multiple values with commas (separated by spaces)
print "line1\nline2\ttab \"quoted\""   # escapes: \n \t \" \\
print "name: {name}, next year: {age + 1}"   # string interpolation — any expression inside {}
let answer = input "Question: "   # numeric input is converted to a number automatically
```
Interpolation rules: literal braces are `{{` / `}}`. A string inside `{}` needs its quotes escaped — `"{join(xs, \", \")}"`. Pulling it out into a variable usually reads better.

## Operators (highest precedence first)
```
- (negation)  →  * / %  →  + -  →  == != < > <= >=  →  not  →  and  →  or
```
- `+` concatenates when a string is involved (`"age: " + 15` → "age: 15") and joins two lists (`[1] + [2]` → [1, 2])
- `*` repeats a string by a number (`"*" * 5` → `"*****"`) — for star patterns, bar charts, separator lines. The count must be a whole number; 0 or less gives ""
- `==`/`!=` work on every type — different types are simply not equal, and lists/dicts/objects compare **by content (deep equality)**. `< > <= >=` are numbers/strings only
- Comparisons don't chain — write `a < b and b < c`, not `a < b < c`
- `%` takes the sign of the **right** operand (the maths and Python convention) — `-7 % 3` is `2`, `-1 % 26` is `25`, so wrapping negatives around (a Caesar cipher, say) just works
- `and`/`or` short-circuit
- `#` starts a comment until end of line. Newlines/indentation are free-form (braces delimit blocks)
- A **trailing comma is allowed** in list and dictionary literals and in argument lists (`[1, 2,]`) — handy when writing them across lines, and the same as Python

## Control flow
```
if x >= 90 then { print "A" }        # then is optional
else if x >= 80 { print "B" }
else { print "F" }

while x > 0 do { x -= 1 }            # do is optional

for i = 1 to 10 { print i }          # both ends inclusive
for i = 10 to 1 step -2 { }          # step direction is inferred when omitted
for x in [1, 2, 3] { }               # iterate a list
for ch in "안녕" { }                  # iterate string characters (UTF-8 aware)
for k in dict { }                    # iterate keys (sorted order)

break    continue
```

## Functions
```
func add(a, b) { return a + b }      # returns 0 if return is omitted
print add(3, 4)
```
Recursion works (depth limit 2000 on the desktop, **200 in the web playground** — a browser's call stack is far shallower. Python's default is 1000, so `topython` raises it with `sys.setrecursionlimit`). Functions can be called before their definition (hoisting).
Functions can read/write global variables; `let` inside a function creates a local.

## Lists (indices start at 1!)
```
let xs = [10, 20, 30]
print xs[1]                # 10  ← the first element is index 1
xs[2] = 99                 # modify
xs[2] += 1                 # compound assignment on elements
let grid = [[1,2],[3,4]]   # nesting
grid[1][2] = 7
let ys = xs + [40, 50]     # + joins lists (new list, originals untouched)
print xs == [10, 99, 30]   # == compares by content (deep equality)
```

`for x in xs` walks **the value as it was when the loop started** — pushing to or
removing from the list inside the loop does not change that pass. (Python's `for` walks
the live list and would loop forever, so `topython` takes a copy where it needs one.)

## Dictionaries (keys are strings only)
```
let d = {"name": "Mir", "age": 15}
print d["name"]
d["school"] = "middle school"   # new keys are created on assignment
d["age"] += 1
print d["missing"]              # error! check with has(d, "key") before reading
```

## Classes
```
class Person {
    func init(name) {          # constructor (optional)
        self.name = name
        self.age = 0
    }
    func greet() { print "Hi, I am " + self.name }
    func birthday() { self.age += 1  self.greet() }   # self accesses fields/methods
}
let p = Person("Mir")          # the class name is the constructor
p.greet()
p.age = 15                     # fields can be read/written/created from outside
print p                        # Person{"age": 15, "name": "Mir"}
```
No inheritance. For dictionaries use `["key"]`, not `.`.

## Built-in functions
```
math:    random(a,b) round(x) round(x,digits) floor(x) ceil(x) abs(x) sqrt(x) min(a,b) max(a,b)
convert: num("15")  str(3)
common:  len(list/string/dict)  reverse(list/string)
list:    push(xs,value) pop(xs) sort(xs) remove(xs,index)  has(xs,value)  find(xs,value)→position (0 if absent)
dict:    keys(d) has(d,key) remove(d,key)
string:  split(s,sep) join(xs,sep) upper(s) lower(s)   # ASCII letters only
         find(s,needle)→position (0 if absent)  replace(s,old,new)  substr(s,start,count)
         s[1] indexing works (read-only)
file:    readfile(path) writefile(path,content) appendfile(path,content) exists(path)
misc:    time()→seconds  exit()→quit immediately  copy(v)→deep copy  error("msg")→raise error
```
- `reverse(list)` reverses **in place** (like `sort`). `reverse("hi")` returns a **new string**, since strings can't be modified.
- To sort descending: `sort(xs)`, then `reverse(xs)`.
- `round(x, 2)` rounds to two decimal places (digits may be 0–15).
- `num(string)` accepts only a number as it looks on the page — surrounding spaces, a sign, a decimal
  point, and an exponent like `1e3`. `0x10`, `inf`, `nan` and `1_000` are all errors: C++ and Python
  disagree on those four, so only what all three accept is allowed. `input` reads by the same rule —
  if it looks like a number it becomes one, otherwise it stays a string.

## Multiple files (import)
```
import "utils.my"        # at the top of the file, one per line. Duplicate imports are skipped
```
Paths resolve **relative to the file doing the importing**, so `venos project/main.my`
works from outside the folder. Subfolders: `import "lib/helpers.my"`.
`import` is desktop-only (not available in the web playground or REPL).

## Error handling (try/catch)
```
try {
    let n = num(input "Number: ")   # error if not a number
    print 10 / n                    # error if 0
} catch err {
    print "Something went wrong:", err   # err holds the error message (a string)
}
error("my own error")               # raise an error that try can catch
```
break/continue/return/exit are not errors — they pass through catch untouched.

An uncaught error stops the program and prints **how execution got there** (interpreter):
```
!! 에러: [줄 2] 인덱스 범위 초과: 10 (리스트 크기: 3, 인덱스는 1부터)
    줄 2 | return xs[10]
    부른 순서: 바깥 (줄 10에서) → 가운데 (줄 8에서) → 안쪽 (줄 5에서)
```

## Copying (mind the reference semantics)
```
let b = a           # for lists/dicts/objects this aliases the same value (mutating b mutates a)
let b = copy(a)     # fully independent deep copy
```

## Limits (you get an error, not a crash)
| What | How much | What you see past it |
|---|---|---|
| Integer precision | about 9 quadrillion (2^53) | It drifts silently — that is what `topython` is for; Python's integers have no limit |
| Function recursion depth | 2000 desktop / 200 web | `함수 호출이 너무 깊습니다 (무한 재귀?)` |
| Expression/block nesting | 200 levels | `식이나 블록이 너무 깊게 중첩되었습니다` — an unclosed bracket lands here |
| List/dictionary nesting | 1000 levels | `자기 자신을 포함한 구조?` when printing, comparing or copying |
| Iterations of one `while` | 10 million, web only (no limit on desktop) | `반복 횟수가 너무 많습니다 (무한 루프?)` |

In the web playground you can **stop a running program at any time with ⏹ Stop (or Esc)** —
whether it is waiting for input or just computing. Output appears as it is printed, too,
instead of all at once when the program ends.

Speed, against CPython running the same program via `topython`: **2-3× slower on
loops and lists, 6× on recursion, about even on dictionaries.** These come from one
machine and one CPython build, so they move around; measuring a different number does not
mean something broke. What matters is the order of magnitude: **at textbook sizes both
finish before you look up.** One exception:
growing a string with `s = s + ch` in a loop is **quadratic in Venos** where CPython
extends in place and stays near-linear. 20,000 characters take 0.07s, so it is invisible
at textbook sizes; 160,000 take 5s. Assembling a very long string is a reason to reach
for `topython`.

## Common mistakes (emphasize these to an AI)

Habits from other languages — `elif`, `!`, `**`, `//` comments, `;`, `:` blocks,
`xs[1:3]`, `"abc".upper()`, `True`/`None` — are named in the error, along with what to
write instead.

1. **Indices start at 1** (not 0!)
2. No `elif` → use `else if`
3. Declare variables with `let` before use (functions are the exception: hoisted)
4. Reading a missing dictionary key is an **error** — check with `has()` first
5. String characters can't be modified → combine `replace()` / `substr()`
6. Check `exists()` before `readfile` (or wrap in try/catch)
7. Don't write `input =` — it's `x = input "Question: "`
8. Logical negation is `not`, not `!`
9. A function or class cannot take a built-in's name (`func floor(n) { }` is an error) — method names are fine
