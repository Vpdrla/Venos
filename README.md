# Venos

[![CI](https://github.com/Vpdrla/Venos/actions/workflows/ci.yml/badge.svg)](https://github.com/Vpdrla/Venos/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

*English | [한국어](README.ko.md)*

**▶ [Try it in your browser — no install needed](https://vpdrla.github.io/Venos/)**

![Demo: playing and building the example RPG](docs/demo.gif)

**Executable pseudocode.** Venos is written the way algorithms are written on a whiteboard or in a
textbook — `if x >= 90 then`, `for i = 1 to 10`, `while x > 0 do` — except you can actually run it.

It is a bridge, not a destination. Beginners get stuck crossing from block coding (Scratch, Entry) to
a real text language: unfamiliar syntax, English-only names, error messages they cannot read. Venos
removes those three obstacles without hiding how programming actually works — and then hands you
over: **`venos topython` rewrites any Venos program as readable Python.**

Implemented in a single C++ file (~5,500 lines) with three backends: a tree-walking interpreter, a
C++ transpiler that produces standalone native executables, and a Python emitter.

```
func fib(n) {
    if n <= 2 then { return 1 }
    return fib(n - 1) + fib(n - 2)
}

for i = 1 to 10 {
    print "fib({i}) = {fib(i)}"    # string interpolation
}

try {
    let n = num(input "Enter a number: ")
    print "10 /", n, "=", 10 / n
} catch err {
    print "Something went wrong:", err
}
```

Identifiers can be written in any language (full UTF-8 support), so classes, functions,
and variables work naturally in Korean, English, or anything else:

```
class 사람 {
    func init(이름) { self.이름 = 이름 }
    func 인사() { print "안녕, 나는 " + self.이름 }
}
사람("미르").인사()
```

## Features

- **Readable syntax** — `if x > 5 then { }`, `for i = 1 to 10`, `while x > 0 do { }`; optional filler keywords (`then`, `do`) make code read like pseudocode
- **Three ways to run** — an interpreter for instant feedback, a transpiler (`.my` → C++ → native executable via g++), and a Python emitter (`.my` → `.py`). All three are differential-tested to produce identical output for the same program
- **A way out** — `venos topython` writes your program as idiomatic Python, so nothing you learn here is thrown away
- **Enough language to write real programs** — functions (recursion, hoisting), classes (constructors, methods, `self`), lists and dictionaries (reference semantics, deep equality with `==`, `+` to join lists, `copy()` for deep copies), string interpolation (`"name: {x}"`), UTF-8-aware string handling, `try/catch`, `import`, file I/O, and 30-odd built-in functions
- **Helpful errors** — messages carry the line number and print the offending line. A misspelled name suggests the closest one (`정의되지 않은 변수: 이릅  (혹시 '이름'?)`), an unclosed `{` points at where it was opened rather than at the end of the file, and an error inside a function shows in one line how execution got there. Habits carried in from another language are named rather than rejected — `elif`, `!`, `**`, `//` comments, `;`, `:` blocks, `xs[1:3]`, `"abc".upper()`, `True` — each with what to write instead. Undefined variables, wrong argument counts and unknown methods are caught at build time
- **Built-in dev environment** — a CLI shell with file management, an editor (arrow-key scroll viewer, paste mode), and one-command run/build

> Note: error messages and shell UI are currently in Korean.

## From Venos to Python

Venos does not want to be your last language. `venos topython` writes the same program as Python you
can read, keeping your variable and function names — Python 3 accepts Korean identifiers, so they
survive the trip:

```
func 최댓값(점수들) {                        │  def 최댓값(점수들):
    let 최대 = 점수들[1]                     │      최대 = 점수들[0]
    for 점수 in 점수들 {                     │      for 점수 in 점수들:
        if 점수 > 최대 then { 최대 = 점수 }  │          if 점수 > 최대:
    }                                        │              최대 = 점수
    return 최대                              │      return 최대
}                                            │
                                             │
let 우리반 = [88, 94, 71]                    │  우리반 = [88, 94, 71]
print "가장 높은 점수: {최댓값(우리반)}"      │  print(f"가장 높은 점수: {최댓값(우리반)}")
```

Both print `가장 높은 점수: 94`. String interpolation becomes an f-string; `then` and the braces
disappear; 1-based indexing is shifted to 0-based and the generated file says so in a header comment,
because that difference is worth learning rather than hiding.

In the playground the same thing is one click: **🐍 Python**.

## Playground

The [web playground](https://vpdrla.github.io/Venos/) runs the full interpreter in your browser via WebAssembly —
including classes, try/catch, and even the example RPG (`input` asks on a line below the output; `import` is desktop-only,
and file I/O writes to in-memory storage that resets on page reload).

It is built to be usable in a classroom where nothing can be installed:

- **🔗 Share** turns your program into a link, so a teacher can hand out a starting point and a student can hand back a result
- **Your work is saved automatically** — closing the tab does not lose it
- **📚 Lessons** walks a beginner through the language step by step, and every lesson has its own link (`#lesson=lists`)
- **🐍 Python** shows the same program in Python and copies it, so a lesson can end in a real Python file

## Learning Venos

**[TUTORIAL.md](TUTORIAL.md)** — 13 lessons from `print` to a small guessing game and out the other side into Python, each one runnable in the
playground with a single click. Korean version: [TUTORIAL.ko.md](TUTORIAL.ko.md).

## Install

Grab a binary from the [latest release](https://github.com/Vpdrla/Venos/releases/latest) — it is
statically linked, so there is nothing else to install.

| Your machine | File |
|---|---|
| Windows | `venos-windows-x64.exe` |
| macOS (Intel or Apple Silicon) | `venos-macos-universal` |
| Linux | `venos-linux-x64` |

On macOS and Linux, make it executable first: `chmod +x venos-linux-x64`.

### Build from source instead

```bash
g++ -std=c++17 -O2 -o venos venos.cpp        # Linux / macOS / WSL
g++ -std=c++17 -O2 -o venos.exe venos.cpp    # Windows (MinGW)
```

No dependencies. Requires C++17 (C++20 compatible), and builds clean with GCC, Clang and MinGW.

## Usage

```bash
./venos                      # interactive shell (create / code / run / build ...)
./venos program.my           # run a file directly (interpreter)
./venos build program.my     # compile to a native executable (needs g++ installed)
./venos build program.my run # compile and run immediately
./venos topython program.my  # write the same program as Python (program.py)
```

The interpreter is self-contained; only `build` shells out to `g++`.

Inside the shell, `repl` starts a line-by-line REPL (type an expression to see its value).

The full language reference: **[VENOS_SPEC.en.md](VENOS_SPEC.en.md)** (English) / **[VENOS_SPEC.md](VENOS_SPEC.md)** (한국어).
The spec is written so you can hand it to an AI assistant and have it write valid Venos code (designed with AI-assisted "vibe coding" in mind).

## Editor support

`vscode-venos/` contains a VS Code extension with syntax highlighting for `.my` files —
copy the folder into `~/.vscode/extensions/` (see its README).

## Examples

**[`examples/algorithms/`](examples/algorithms/)** — fifteen textbook algorithms (selection
sort, binary search, Euclid's algorithm, the sieve, Hanoi, a Caesar cipher…), each written
line-for-line against the pseudocode a textbook prints, with that pseudocode in a comment
at the top. Every one of them is run three ways by the test suite — interpreter, compiled
binary, and the Python `topython` emits — and all three must agree.

```bash
venos examples/algorithms/binary-search.my
venos topython examples/algorithms/binary-search.my    # the same program in Python
```

A text RPG exercising classes, dictionaries, string interpolation, and save/load via file I/O — in two flavors:
`examples/rpg.en.my` (English identifiers, the one in the demo above) and `examples/rpg.my` (the same game written entirely with Korean identifiers):

```bash
venos examples/rpg.en.my
venos examples/rpg.my
```

## Architecture

```
source → lexer (tokens) → recursive-descent parser (AST) ─┬→ tree-walking interpreter
                                                          ├→ C++ code generation → g++ → native executable
                                                          └→ Python emission (`topython`)
```

Everything — lexer, parser, AST, interpreter, transpiler, runtime library, and the CLI shell — lives in the single file `venos.cpp`.

## Why

**Where Venos belongs, and what it refuses to become: [STRATEGY.md](STRATEGY.md)**
([한국어](STRATEGY.ko.md)) — what other teaching languages got right and wrong, and the
gap this one is aimed at.

**[DEVLOG.md](DEVLOG.md)** ([한국어](DEVLOG.ko.md)) — the bugs this took, and the instrument
that found each one. A `windows.h` macro from 1985, a `return` that threw a C++ exception
on every call, a parser with no depth limit, and an `import` that had never been tested.

I built this from scratch to understand how programming languages actually work.
It started as v0.1 (an interpreter that could only do variables and `print`) and grew to v0.6
by writing real programs in it, finding what was missing, and adding it — the text RPG in
`examples/` was the validation project that drove features like `exists()`, `try/catch`, and `import`.

## Testing

Every language feature is verified by **differential testing**: each program in `tests/cases/` — and
every example in `examples/algorithms/` — runs on all three backends (the interpreter, the transpiled
native binary, and the Python emitted by `topython`) and the outputs must match. This is what has
caught most of the code-generation bugs.

Four more phases run alongside it:

- **Error messages.** `tests/diag/*.my` are wrong on purpose and their output must match `.expected`
  character for character. What a beginner reads when something breaks is a feature, so it is tested
  like one.
- **Refusals.** `tests/nopython/*.my` are the places Python would answer differently from Venos, and
  `topython` must refuse them with a line number. Emitting wrong Python is worse than refusing,
  because the student has no way to know it is wrong.
- **Name cross-check.** `tools/check-builtins.js` pulls the builtin list out of all five places it
  lives (three backends, the typo-suggestion table, and the VS Code grammar) plus the keywords, and
  fails if they disagree. Every way of forgetting one of them is silent.
- **Lesson track.** `tools/check-lessons.js` runs every lesson's code in both languages, checks that
  names the prose points at exist in the code, and that the generated tutorials are up to date.

Separately, `tools/sanitize.sh` builds with ASan and UBSan and sweeps the whole corpus through both
the interpreter and the *generated* C++ — the emitted runtime is a second implementation, so it gets
compiled with sanitizers and run too — then fuzzes mutated programs looking for crashes and hangs.
That is what found the parser's missing depth limit.

CI runs all of it on every push, on Linux and on Windows, in a real browser for the playground, and
separately verifies that the playground WASM committed in `docs/` was built from the current
`venos.cpp`:

```bash
tests/run_tests.sh
```

## License

[MIT](LICENSE)
