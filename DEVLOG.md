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

### The fifth one the runner saw, and passed anyway

Much later, while fixing something unrelated, a fifth turned up. The interpreter was returning
**exit code 0 for an uncaught error** — where the compiled build of the same program returned 1. A
grading script would read a dead program as a pass. I fixed it, pushed, and the Windows job went
red for the first time.

That job **already had** a step running `tests/.tmp_한글 폴더/정렬.my` through all three paths.
And the `venos build` step had been failing every single time — `cc1plus: fatal error: í•œê¸€ ...:
No such file or directory`. Of course it was: g++ gets its argv through the ANSI code page too. But
cmdBuild returned 0 no matter what, so the error text sat in the CI log under a green check.

So this one was not hidden for want of an instrument. The instrument existed, ran on every push, and
was pointing straight at it. I had flattened its signal to zero. If the lesson of the nine chapters
around this one is *build the next instrument*, the lesson here is cheaper: **check that the
instruments you already have can say the word "failed."** Green means it passed, not that anyone
looked.

(The fix: when the path has non-ASCII in it, step into the folder and call g++ with ASCII-only
temporary names, then rename the result into place. I cannot fix g++, so I stopped putting Korean in
its argv. The CI step now also checks that the built program printed the number it was meant to
compute.)

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

## 10. The tenth instrument, built the same week

Writing §8 made the shape obvious enough that I went and built the next one
before finishing the essay.

The fuzzer in §7 mutates real programs, so nearly everything it produces is a
syntax error. It can find a crash; it cannot find a *wrong answer*. So the
other half: a generator that writes random but **valid** Venos programs from a
grammar — nested ifs, loops with `break` and `continue` inside `try`, string
repetition inside list indices, dictionary iteration, a class with a
constructor and methods — and runs each through all three backends, diffing the
output.

The programs are constrained to finish without error, because error text
legitimately differs between backends and comparing it would produce nothing
but false alarms. What is left is the question that matters: given a program
that works, do the three implementations agree?

Its seventh program did not, and the disagreement was three ways:

```
try { 없는변수 = 3
      print "여기 안 옴" } catch 오류 { print "잡음" }
```

The interpreter raises where the line runs and the `catch` catches it.
`venos build` refuses at build time, as designed. And `topython` emitted Python
that **assigns the variable**, because in Python assignment creates a name. A
different program, from a mistake the language is otherwise good at catching.

Its ninth program hung — the Python hung, while Venos finished:

```
let xs = [1, 2, 3]
for x in xs { push(xs, x * 10) }
```

Venos walks the list as it was when the loop started. Python's `for` walks the
live list, so it walks the appended elements too, forever. No error anywhere,
just a different answer, from a program with nothing wrong in it. Appending to
the list you are looping over is something a beginner does on purpose.

That one has the nicer fix. The emitter now writes the loop body first and
wraps the iterable in `list(...)` only if that body mentions the name — across
the sixteen algorithm examples, that is zero loops. `for 점수 in 점수들:` stays
exactly as it was, which is the whole point of the feature.

### And then again, the same day

Days later, poking at builtins by hand, I found two more. `has("abc", "b")` is an
error in Venos and `1` in Python. `sort([[2],[1]])` is the same trade, an error
for an answer. I fixed both, and only afterwards had the thought I should have
had first: **anything I can find by poking, the generator should find while
nobody is watching.**

So it got one more kind of line: nineteen builtins crossed with twenty-two
deliberately awkward arguments -- empty strings, nested lists, `"0x10"`, a list
where a dictionary belongs -- wrapped in `try/catch` and collapsed to the single
character `"E"`. The wording stays invisible; "error or value" becomes
comparable. Generated programs had always been constrained to run without
errors, because comparing error text across backends produces nothing but false
failures. This unties that knot in exactly one place.

**Its fifteenth program** was `join("abc", "-")`. Python's `str.join` iterates
its argument and a string iterates by character, so a call Venos rejects comes
back holding `"a-b-c"`. I had walked down the same table by hand and gone
straight past it.

The new instrument also brought a new false positive with it. When the generator
wrote `max([[1],[2]], "1e3")`, `topython` refused it **by design**, and the
comparison counted the refusal as a mismatch. CI went red and all three of that
run's mismatches were exactly that. It is the same mistake as `fuzz.py` counting
every timeout as a hang, made twice in one day -- **before you switch an
instrument on, you cannot see what it will get wrong either.**

Two bugs, both of the kind nothing else I had built could see. Which is the
argument of this essay arriving on schedule, and I would rather report that
than pretend I planned it.

---

## 11. An instrument I already had, and a question I had never asked it

The eleventh instrument was not built. It was a question I had never put to one I already had.

The sixteen textbook algorithms had been in the suite for a long time. Run three ways, outputs
compared, green on every push. But the only inputs they ever saw were the ones I had written
down. I had watched `selection_sort([64, 25, 12, 22, 11])` a dozen times. I had never once run
`selection_sort([7])`.

    !! error: [line 6] index out of range: 2 (list size: 1, indices start at 1)

**All seven** sorts and searches died on a one-element list. One cause, and it was not in any of
the algorithms. In `for i = 1 to n - 1`, when `n` is 1 the range becomes `1 to 0` — and Venos's
`for` was written to **flip direction** when the start is past the end. I had added the line as
a convenience:

    stepv = (s.num <= e.num) ? 1 : -1;

So `for i = 1 to len(A)` on an empty list did not run zero times. It ran **twice**, with `i = 1`
and then `i = 0`, and 0 is not an index here. A textbook's `for i ← 1 to n`, Pascal's `for` and
Python's `range` all agree that `n < 1` means no iterations. Only mine disagreed, and my entire
example collection was standing on the difference.

The fix was three lines. Deciding to make it was the hard part, because it changes what the
language means. Two facts made it easy. Every descending loop in the repository **already wrote
`step -1`** — not one line relied on the inference. And the loops lesson already taught "counting
down needs a negative `step`". A convenience nobody used was making every textbook algorithm
wrong at its boundary.

The bonus was larger than the fix. With direction settled at compile time, `topython` can emit a
real `range()` instead of a helper call: `_rng` went from 42 sites to 10, and `for i = 1 to n - 1`
now comes out as `range(1, n)`, which is the shape the Python textbook prints.

Then I asked the same question of the other sixteen. Every function, at 0, at 1, at empty, at
negative, and just outside its domain. Ten of them answered wrong.

`to_base(7, 1)` **ran forever** — `n % 1` is 0 and `n / 1` is `n`, so the loop never advances.
That is what a textbook's "2 ≤ base" is protecting. `sieve(0)` read `remaining[1]` of an empty
list. `transpose([])` read `len(A[1])`. `moves(-3)` fell past its `n == 1` floor to the recursion
limit.

Three were worse than crashing. `multiply(2×3, 2×3)` **returned a plausible 2×3 of zeros** —
nothing ever checked A's column count against B's row count. `to_base(-26, 2)` returned `""`.
`from_base("9", 2)` returned `9`. There is no 9 in base 2.

And three disagreed **with themselves**. `caesar(caesar("Hello", 5), -5)` gave `"hello"` — a file
whose whole point is demonstrating decryption, and every character went through `lower()` and came
back lowercase. `moves(0)` crashed while `moves_iterative(0)` returned 0, for the same quantity.
`gcd(0, 5)` was 5 and `brute_force(0, 5)` was 1, in the same file.

The last one was not a bug but a property of the algorithm, so instead of fixing it I said it out
loud: run-length encoding cannot round-trip text containing digits. `compress("a1a")` is `"a111a1"`,
which decodes to 112 characters. The file already checked its own round-trip — but not one of its
five samples had a digit in it.

All sixteen files now end with a boundary section. The call that was broken is in it, and the
three-way suite holds it there. The collection is not decoration; it is the **specification** of
"a textbook algorithm works here", and a specification only specifies what it actually runs.

## 12. The source does not come from a keyboard

Everything up to here treated the program as something the student typed. Then I wrote a file the
way a student actually gets one — pasted out of a blog, saved from Notepad, handed over as a link —
and none of it ran.

    let x = 1        →  정의되지 않은 변수:  1
    let x = 1        →  = 기호가 필요합니다
    print “한글”     →  정의되지 않은 변수: “한글”
    let a = 5 – 2    →  = 기호가 필요합니다

Four lines that are correct on screen. The first has U+00A0 where the space should be, because a web
page put it there. The second starts with a BOM, because that is what Notepad writes. The third has
the quotes Word substitutes, the fourth the dash Word substitutes. The lexer treated every byte
above 0x7F as part of an identifier, so all four vanished into a name and the error pointed at a
line with nothing wrong in it. There is no move the student can make here: you cannot delete a
character you cannot see.

The lexer carries a table of thirteen of them now, and names each one with what to type instead. A
BOM at the very start of a file is skipped rather than reported — it is not a mistake, it is what
the editor wrote. Inside a string literal none of them are touched, because there they are data.

The same day produced two more of the same kind, neither of them about the language:

    venos examples/   →  "파일 없음: examples/.my"
    venos 정렬.my     →  "파일 없음: 정렬.my"

The first is the slash tab-completion adds, quoting a filename the student never typed. The second
is Notepad again: saving as `정렬.my` writes `정렬.my.txt` unless you change the file-type box, and
Windows hides the extension, so the file is sitting right there in the folder. Both say what
happened now.

And the playground: a teacher hands out a program as a link, a messenger cuts the URL at some
length, and the page fell back to the student's own autosaved work **without a word**. There is a
perfectly good program on screen; they have no reason to doubt it is the one they were sent. It
says so now — and checking that exposed a hole in the checker itself, which had been reading an
editor it had filled in moments earlier, because navigating to a URL that differs only in the hash
is not a reload.

## 13. A feature that arrived with its own invariant

Korean textbooks teach algorithms with a 추적표 — a table the student fills in by hand, line by
line, with what each variable holds. The algorithms they use it on are the sixteen in
`examples/algorithms/`. Portugol Studio, the language this project took its position from, ships a
debugger with a variable pane for the same reason. Venos had nothing: the program runs, and the
values are invisible.

    5| -> 팩토리얼(4)
    3|   -> 팩토리얼(3)
    3|   <- 팩토리얼 = 6
    5| <- 팩토리얼 = 24
    5| 답 = 24

What makes this chapter belong in an essay about instruments is not the feature. It is that the
feature is only safe if something holds it to a rule, and the rule was obvious enough to write down
first: **the answer with the trace must equal the answer without it**, character for character, on
every case in the suite. Trace lines go to stderr so the comparison is trivial to make.

That rule failed on its first run. Printing a value that contains itself throws — Venos refuses
cyclic structures — so the trace threw while trying to show one, and killed the program it was
supposed to be watching. A feature whose entire job is to observe without disturbing, disturbing.
One minute old, caught by its own invariant, and I would not have thought to try it.

The rest of the round was the chapter 11 question asked again, family by family: hand the builtins
inputs they have never been given. Ninety-five shapes across file, math, string, list, dictionary,
object, control flow and interpolation. Ninety-three already agreed across the three backends,
which is the result worth having and not one to assume. The two that did not:

- `exists(".")` was 0 in both Venos backends and 1 in the generated Python, because
  `os.path.exists` counts directories. Not an error becoming a value — a *different answer*, in a
  program that checks for its save file before branching.
- `num("1e300") * num("1e300")` **killed the generated program**: `OverflowError: int too large to
  convert to float`, on a line that looks fine, from a helper the student never wrote.

Ninety-three agreements are now cases in the suite. That is the part that looks like nothing and is
not: an agreement that is only remembered is an agreement that will break quietly.

## 14. A check that compared pixels instead of strings

Venos has told you **which line** for a long time. It had never told you **where in the
line**. rustc does it, elm does it, so does any clang built this decade:

    !! 에러: [줄 6] if 조건에서 값을 견줄 때는 == 를 씁니다 (= 는 값을 넣을 때)
        줄 6 | if 학생수 = abc { print "같다" }
                        ^

All three backends share one parser, so fixing it in one place fixed all three — the
interpreter, `build` and `topython` mark the same character. (The last two didn't at first:
both `catch` blocks passed `e.what()` and dropped the column on the floor. Same shape as a
check that exists in only one backend, except this time it was the information, not the
check.)

There was a new kind of mistake hiding in it. Aligning `^` by **character count** goes
wrong on every Korean line, because a Hangul syllable takes two terminal columns. I knew
that and handled it, then verified all twenty-eight cases against Python's
`east_asian_width` — an independent implementation. Zero mismatches.

But the playground is not a terminal.

`Cascadia Code` has no Hangul. The browser draws the Latin characters in it and hands the
Hangul to a fallback font, and **nothing guarantees that font's Hangul is exactly twice a
Latin advance.** The output string is identical to the native one, character for character.
It even looks plausible. So no string comparison anywhere could see this.

I wrote the check against **coordinates** instead. In the browser, measure
`getBoundingClientRect().left` of the character the caret should point at, and of the caret
itself. First run:

    ✗ 에러의 ^ 가 어긋남  (가리킬 자리 820.1px, 캐럿 831.5px)

About a character and a half. To a student it is pointing at the wrong thing. The fix lived
in the same place: undo the column count with the same rule to recover the character index,
then place the caret at **that character's measured x**. Correct in any font.

The twelfth instrument does not read the screen. It measures it. The other eleven all
compared text, and every one of them was blind to a place where the text is right and the
picture is wrong.

## 15. The instrument's own shape is the blind spot

Two defects turned up the same day for the same reason.

Open one stray `{` in the REPL and the continuation prompt swallows everything
after it — `quit`, `:q`, `나가기`, all of it. At a real terminal the only way out is
Ctrl+C, which takes the shell with it.

The suite **could not see this**. Every REPL check feeds stdin from a pipe, and a
pipe ends in EOF, and EOF breaks that loop on its own. Not a weak check — a bug
that does not exist under the input the check uses. It exists only where a human
is typing.

Second one, same day. `tests/cases/utf8.my` puts emoji in strings and dictionary
keys and compares three ways. An earlier round — "hand the UTF-8 path emoji and
combining characters" — came back **zero**, and that was true, for the places it
handed them to. One place had never been tried: **a name**.

    let 🍎 = 3

Runs in the interpreter. Runs in the compiled binary. `topython` emitted `🍎 = 3`
and Python could not **parse the file at all**. Worse than a wrong answer — the
.py the student receives does not run a single line.

Chapter 11's question — what input has this instrument never been handed? — only
counts inputs the instrument *can* be handed. These two sit outside it. The REPL
check cannot impersonate a terminal, and the UTF-8 case thought of the character
as **data** and never as a **name**. It is not that the instrument missed
something. Its *shape* could not contain it.

So the question changes: what situation can this check **structurally never
produce**?

## What the list adds up to

Twelve instruments, and each one found the class of bug only it could find:

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
| a generator of valid programs | two answers that differed with no error in sight |
| a file written the way students get one | four correct-looking lines that could not run |
| an invariant shipped with the feature | the trace killing the program it was watching |
| a check that measures what was drawn | the right characters pointing at the wrong place |
| asking what the instrument cannot produce | a state no pipe can reach, a place never tried |

The pattern is not "test more." It is that each instrument sees one kind of thing and is
blind to the rest, and you cannot reason your way to the blind spots — you can only build
the next instrument and find out. I could have read the parser a hundred times without
noticing it had no depth limit, because nothing in the code says "this is missing." The
fuzzer said it inside its first fifty mutated programs.

The corollary, which I like less: whatever is broken in Venos right now is broken in a way
I cannot currently see, and the way to find it is not to look harder. It is to build the
next instrument.

The coda to chapter 5 is the cheap corollary to the expensive one. Building the instrument is not
the end of it — you also have to check that it can **say the word "failed."** One green check sat on
top of an error message for the better part of a year.

Section 10 is that corollary being tested while the essay was still open, which is the
most convincing version of it I could have hoped for and the least deliberate. Ten
instruments, ten kinds of blindness, and the tenth found two things nine could not.

And section 11 turned the corollary over. The eleventh was **not an instrument.** It was one
question, put to an example collection that had been green for the better part of a year — what
happens when there is only one element? Ten of the sixteen answered wrong, and one of the answers
was a rule of the language itself.

So the lesson on the list needs a second line. Building the next instrument is one way. Counting
**which inputs you have never handed to the instruments you already have** is another. My
examples only ever saw the inputs I wrote down, and that list contained neither `[]` nor `[7]`.
Nowhere in the code did it say so.

The twelfth is not something I can build. Nobody who is not me has written a Venos
program yet.
