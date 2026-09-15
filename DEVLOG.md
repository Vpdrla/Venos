# Every bug I found, I found by building something that could see it

*English | [한국어](DEVLOG.ko.md)*

> A draft. Notes from building [Venos](https://github.com/Vpdrla/Venos), a small
> programming language that reads like textbook pseudocode and can rewrite itself as
> Python.

I did not set out to write an essay about testing. I set out to write a language, and I
kept a list of the bugs I found and how I found them. When the list got long enough, it
said something I did not expect: almost none of these were found by reading code or by
thinking harder. Each one appeared the day I built an instrument that could see that
*kind* of bug, and none of them were visible the day before.

Here is the list, in the order the instruments arrived.

---

## 1. Two backends, one program, diffed

Venos has an interpreter and a transpiler to C++. That means every language feature is
written twice, and the second one can be wrong in ways the first one is not.

So the test suite runs each program both ways and diffs the output. Not "does it produce
the expected value" — just "do the two implementations agree." I did not have to write
down what the right answer was; I only had to have two opinions and notice when they
differed.

This found code-generation bugs steadily, which is what you would expect. What I did not
expect is that it kept finding them for months. Every time I added a feature, the diff
caught the half of it I had implemented carelessly, usually within a minute of writing
it. A third backend — a Python emitter — later joined the diff, and it was worse than the
C++ one: it had bugs the C++ backend did not, because Python has opinions of its own.

The cheapest testing idea I know of. No expected outputs to maintain, no assertions to
write, and it gets stronger every time you add an implementation.

## 2. `IN` and `OUT`

Windows's `windows.h` defines `IN` and `OUT` as empty macros. My token enum had `IN` in
it, for `for x in xs`.

The error message this produces is not "you cannot name a token IN". It is a wall of
syntax errors starting a hundred lines after the real cause, because the preprocessor
deleted the word and left the commas.

The fix is to rename the enumerator to `INKW` and `#undef IN` after the include. The
lesson is that the bug was not in my code at all — it was in the interaction between my
code and a header written in 1985 — and no amount of staring at my own file would have
shown it. Compiling on Windows showed it in one step.

Which raises the obvious question, answered much later: what *else* was Windows doing?

## 3. A segfault that was really a recursion limit

Deep recursion crashed. Not "raised an error" — crashed, no message.

The interpreter recurses on the C++ stack, a few kilobytes per Venos call. Linux gives a
thread 8MB by default, Windows 2MB. My recursion limit of 2000 was set by guessing, and
the guess was above where the real stack ran out, so the limit never fired.

The fix I like: run the program on a dedicated thread with a 128MB stack. Now the
language's own limit is always reached first, and a student who writes an infinite
recursion gets a Korean sentence instead of a crash.

Later the same bug came back wearing a different hat. In the browser, the WebAssembly
build has no threads, and — this took me an embarrassingly long time — `TOTAL_STACK` in
Emscripten sizes the *shadow stack in linear memory*, not the JavaScript engine's call
stack. The engine's stack is much shallower, and the student got
`RangeError: Maximum call stack size exceeded`, in English, from a language whose entire
point is that the errors are readable.

Measuring where it actually breaks was strange: 450 frames passed twice and then 380
failed. The crossover moves between runs, and once a page overflows it does not recover.
So the web limit is 200, comfortably under every failure I saw and far past anything a
textbook asks for — Hanoi, Euclid and binary search are all under thirty deep.

## 4. Exceptions are not free, and `return` is not rare

`fib(27)` took 2.7 seconds. CPython does it in 0.039.

Being slower than CPython is fine; a tree-walking interpreter against a bytecode VM is a
few times slower and that is the deal. Seventy times slower is not that deal, it is a
bug.

The isolating experiment took two minutes: the same recursion written without a `return`
ran in a quarter of a second. `return` was implemented as a C++ exception, thrown and
caught on every single call.

Exceptions are cheap when they do not happen. `return` always happens.

Replacing them with a `Flow { NORMAL, BREAK, CONTINUE, RETURN }` return value, threaded
through every statement, took an afternoon and a careful test file full of `break` inside
`try` inside a loop inside a method. Measured on one machine, the commit before against
the commit after:

| | before | after | CPython |
|---|---|---|---|
| `fib(27)` | 2.73s | **0.23s** | 0.039s |
| `hanoi(20)` | 10.58s | **1.93s** | — |
| 3M-iteration loop | 0.222s | 0.215s | 0.234s |

The last row is the control: nothing there got faster, because nothing there was
throwing. Without that row the numbers would be a story I was telling myself.

Six times CPython is the deal. I will take the deal.

## 5. The day I turned on a Windows runner

The release workflow had been shipping a Windows `.exe` for a while. Nobody had ever run
one — the binary was cross-compiled on Linux and uploaded.

I added a `windows-latest` job that builds with MinGW and runs the whole suite. It found
four real bugs on its first four runs, and every one of them is invisible on Linux:

1. **Korean filenames did not work.** Windows hands `main()` its argv in the ANSI code
   page, so `venos 정렬.my` arrives as `venos ??.my`. For a language whose selling point
   is that Korean speakers can name things in Korean, the Windows build could not open a
   file named in Korean.
2. **The transpiled binary could not open Korean paths either** — it used a narrow
   `std::ifstream` where the interpreter converted to wide.
3. **Every file predicate was true in the transpiled binary.** Constructing
   `std::ifstream` from a `std::filesystem::path` reports success on a file that does not
   exist under MinGW. So `exists()` said yes to everything, and `readfile()` never raised.
4. **The emitted Python crashed printing Korean**, then mangled Korean input, because
   Python on Windows encodes stdout with the locale code page.

Four bugs, all in the same place: the boundary between my UTF-8 world and the platform's.
They had been shipping in releases for weeks. The runner cost twenty lines of YAML.

## 6. The Python it emitted was a different program

`venos topython` rewrites a Venos program as Python. It is the reason the language can
claim to be a bridge rather than a destination — a language that digs its own exit cannot
quietly become somewhere you stay.

Which makes emitting *wrong* Python the worst thing it can do. A refusal is honest. A
plausible-looking translation that answers differently is a trap, because the student has
no way to know.

Reading the generated files found four:

```
Venos prints              the generated Python printed
평균: 83.3333             평균: 83.33333333333333
값: 1                     값: True
딕: {"a": 1}              딕: {'a': 1}
-7 % 3 → 2                -1
```

`print` routed values through a formatting helper; f-strings did not. And `%` used C's
`fmod`, which takes the sign of the left operand — so a Caesar cipher, the textbook
example that exists precisely to wrap a negative number around, gave different answers
depending on which backend ran it.

Then I stopped reading and wrote a probe that ran the same edge cases through all three
backends and diffed them. It found five more, and these were nastier, because they were
all cases where Venos raises an error and Python quietly returns a value:

| Venos | the Python it emitted |
|---|---|
| `xs[0]` → error, indices start at 1 | **the last element** |
| `replace(s, "", r)` → error | `-a-b-c-`, inserted between every character |
| `find(s, "")` → error | `1` |
| `substr(s, 0, n)` → error | `""` |
| `min("a", "b")` → error | `"a"` |

`xs[0]` is the one that bothers me. "Indices start at 1" is the first item on the spec's
list of common beginner mistakes — we *know* students write `xs[0]` — and the translation
turned a caught mistake into a wrong answer, because Python reads a negative index as
counting from the end.

The fix has a shape worth naming. Where the offending value is written in the source,
`topython` now refuses, with a line number. Where it is a variable, the call routes
through a helper that enforces the Venos rule at run time. But for a variable *index* it
keeps emitting `A[i - 1]`, because the alternative turns insertion sort from

```python
while j >= 1 and A[j - 1] > 키:
    A[(j + 1) - 1] = A[j - 1]
```

into a page of `_at(A, j)` calls, and emitting Python a student can read is the entire
point of the feature. So the generated file's header says what the limitation is. Not
every hole should be plugged; some should be labelled.

## 7. A fuzzer, and a crash that only happened on the way out

Students mistype. Constantly. So I pointed a small mutation fuzzer at the corpus — flip
bytes, delete chunks, insert stray braces, stack three thousand open parens — and ran it
against a build with AddressSanitizer and UndefinedBehaviorSanitizer.

The sanitizers found nothing, across the whole corpus, through the interpreter *and*
through the generated C++ compiled with sanitizers of its own. That was a good day.

The fuzzer found one thing, and it was a good one: the recursive-descent parser has no
depth limit, so `print ((((((…` recurses until the stack is gone.

The interesting part is *where* it crashed. Running a file was fine to tens of thousands
of levels — because of the 128MB stack thread from §3. But `topython` and `build` parse on
the main thread, and died at five thousand. The same file ran and could not be converted.

One depth limit in the parser fixes all of it, and more than I expected: capping parse
depth caps AST depth, which caps the evaluator's recursion, the code generator's, the
Python emitter's, and the AST destructor's. One limit instead of five.

Two more things fell out of the same input. The source line printed under an error was
never truncated, so the five-thousand-paren case dumped 197KB and pushed the actual
message off the screen. And an expression inside a string interpolation is parsed by a
sub-parser whose line numbers start again at 1, so an error in `print "{없는변수}"` on
line 4 reported line 1.

## 8. Asking what the tests were *not* looking at

Late on, I did something I should have done much earlier: instead of writing more tests,
I asked which features had none.

`import` had zero. Not one program in the differential suite used it.

So I wrote one, and it failed immediately — not because of a subtle bug, but because
`import "utils.my"` was looked up in the current working directory rather than beside the
file containing the import. A project folder only worked if you happened to be standing
inside it:

```
$ venos 프로젝트/main.my
!! 에러: import 실패: 파일을 열 수 없습니다: utils.my
```

with `utils.my` sitting right there next to `main.my`. Every language resolves imports
relative to the importing file. Mine had never been asked.

The same audit produced a case covering the filler keywords, escapes, assignment through
a nested path, objects stored in lists, fields attached after construction, trailing
commas, operator precedence — and all three backends already agreed on every line of it.
That is the result I wanted. It was worth nothing until it was checked.

Coverage audits find bugs at a much better rate than writing more tests of the things you
already test. It is uncomfortable, because the question is "what have I been avoiding?"

## 9. Errors, or: what a beginner actually types

The language is aimed at students crossing from block coding to Python, which means they
arrive carrying another language's habits — mostly Python's, which they are on their way
to learning anyway.

I typed twenty snippets the way such a student would, and read what came back:

```
print 2 ** 3        →  문법 오류: 값이 와야 할 자리입니다
// 주석             →  문법 오류: 문장이 될 수 없는 토큰입니다
print !true         →  알 수 없는 문자: '!'
} elif x == 2 {     →  문법 오류: = 이(가) 필요합니다
print True          →  정의되지 않은 변수: True
"abc".upper()       →  문자열에는 메서드를 호출할 수 없습니다
```

Every one of those is true, and not one tells a thirteen-year-old what to type instead.
Two of them — `elif` and `!` — are literally on the spec's list of common mistakes. We
knew they would happen and said nothing useful when they did.

They now name the habit and the replacement: *"거듭제곱 연산자는 없습니다 — x * x 로 쓰거나
반복문으로 곱하세요"*, *"주석은 // 가 아니라 # 로 씁니다"*, *"elif 는 없습니다 — else if 로
쓰세요"*. It is a lookup table and a handful of lexer cases. It took an afternoon. The only
hard part was deciding to look.

There is a version of this that is worth stating plainly: **error messages are a feature,
so test them like one.** The suite has thirty-five programs that are wrong on purpose, and
their output must match a recorded file character for character. When I improve a message
I have to update the file, which is exactly the friction that keeps me from changing
wording carelessly.

---

## What the list adds up to

Nine instruments, and each one found the class of bug only it could find:

| instrument | what it found |
|---|---|
| two backends diffed | codegen bugs, continuously |
| a third backend | the promise being broken quietly |
| compiling on Windows | a 1985 header, and four UTF-8 boundary bugs |
| a big stack + a real limit | crashes turned into sentences |
| a benchmark with a control row | `return` costing a microsecond each |
| a fuzzer + sanitizers | a parser that segfaulted on nested parens |
| a real browser in CI | a JavaScript error where Korean should have been |
| a touch viewport | 34px buttons in a classroom of tablets |
| a coverage audit | `import` had never been tested at all |

The pattern is not "test more." It is that each instrument sees one kind of thing and is
blind to the rest, and you cannot reason your way to the blind spots — you can only build
the next instrument and find out. I could have read the parser a hundred times without
noticing it had no depth limit, because nothing in the code says "this is missing." The
fuzzer said it inside its first fifty mutated programs.

The corollary, which I like less: whatever is broken in Venos right now is broken in a way
I cannot currently see, and the way to find it is not to look harder. It is to build the
tenth instrument.

I have a guess about what it should be. Nobody who is not me has written a Venos program
yet.
