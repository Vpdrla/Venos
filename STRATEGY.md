# Where Venos Belongs

*English | [한국어](STRATEGY.ko.md)*

This document answers one question: **what is Venos for, and what should it refuse to become?**
It is written from research into how other languages found — or lost — their place.

## 1. Every surviving language owns one place

Not "is good at many things." Owns one place, where it is the path of least resistance.

| Language | The place it owns | What it pays for it |
|---|---|---|
| Python | Data, ML, and teaching | Slow; packaging is a maze |
| JavaScript / TypeScript | The only language a browser runs | Accumulated warts, churn |
| C | Talking to hardware | No safety net |
| C++ | Games, high performance | Enormous complexity |
| Java / C# | Enterprise, Android, Unity | Ceremony |
| Rust / Go | Safe systems / simple services | Learning curve / expressiveness |
| SQL | Asking questions of data | Not general purpose |
| Scratch, Entry | A child's first contact with code | **Does not carry over to text** |

A language without such a place does not survive on merit. It has to be the obvious choice
*somewhere*, or it is never the choice anywhere.

## 2. The instructive failures

**Pascal** was designed as a teaching language and was very good at it. Then it tried to become a
serious general-purpose language, and Brian Kernighan's [*Why Pascal is Not My Favorite Programming
Language*](https://www.cs.princeton.edu/~bwk/btl.mirror/notfavorite.html) documented, point by
point, why it could not be. It lost the second fight and the first one along with it. Today
beginners start on Scratch and move to Python; Pascal is gone from most curricula.

**Korean-keyword languages** (약속, 와글, 새싹) translated the keywords into Korean and required an
install. Translating keywords is a dead end — it makes the transition *harder*, because none of the
structure transfers. Requiring an install is fatal in a school where nothing can be installed.

The lesson from both: **a teaching language dies when it forgets it is temporary.**

## 3. The instructive successes

**[Portugol Studio](https://github.com/UNIVALI-LITE/Portugol-Studio)** (UNIVALI, Brazil) is a
Portuguese pseudocode language with **90,000+ downloads**, used across Brazilian universities and
technical schools. It succeeded because it declared itself a bridge and *named the exit*: courses
introduce Java around week four. It never claimed to be the destination.

**[HAGGIS](https://en.wikipedia.org/wiki/Haggis_(programming_language))** is the reference
pseudocode of Scotland's SQA exams — a single notation shared by every school, examiner, and pupil,
used to *read* code in exam questions rather than to write programs. Notably, the SQA never shipped
an interpreter, so third parties built one. **Pseudocode that cannot run creates demand.**

## 4. The gap in Korea

Three facts line up:

- Under the 2022 revised national curriculum, informatics hours **doubled** — elementary 17 → 34,
  middle school 34 → 68 — phased in from 2025 for the first year of middle and high school
  ([source](https://www.kyobit.com/news/articleView.html?idxno=2308)). Demand is growing right now.
- Korean textbooks teach algorithms through **flowcharts and pseudocode**, and **that pseudocode
  cannot be run.** There is no Korean equivalent of HAGGIS — no shared notation, and no interpreter
  for one.
- The difficulty of moving from block-based to text-based programming is
  [well documented](https://www.sciencedirect.com/science/article/pii/S2590118425000280): typing and
  spelling, undecipherable errors, and a shift in the programming model all land at once.

And Venos already looks like the thing textbooks write: `if x >= 90 then`, `for i = 1 to 10`,
`while x > 0 do`. That is not a coincidence worth ignoring.

## 5. The position

> **Venos is executable pseudocode, and a bridge from block coding to Python.**

It follows the Portugol model and adds what Portugol lacks. A Portugol student rewrites their
program by hand when moving to Java. A Venos student runs `venos topython` — or clicks **🐍 Python**
in the browser — and gets the same program in idiomatic Python, with their own variable and function
names intact.

That single feature is also a structural commitment: a language that ships the exit ramp cannot
quietly turn into Pascal.

Three obstacles removed, none of the substance removed:

| Obstacle | What Venos does | What it deliberately does *not* do |
|---|---|---|
| Unfamiliar syntax | Reads like textbook pseudocode | Not blocks — you type real code |
| English-only names | Identifiers in any language (Korean, etc.) | **Keywords stay English**, so the structure transfers |
| Unreadable errors | Errors in Korean, with line numbers and the offending line | Does not hide that errors exist |
| Nothing can be installed | Runs entirely in the browser | Also ships real native binaries |

## 6. What this means for what gets built

**Build:** anything that makes textbook algorithms expressible, runnable, and translatable —
the Python exit ramp, worked textbook algorithms ([`examples/algorithms/`](examples/algorithms/)),
better beginner error messages, classroom plumbing (share links, lesson tracks).

**Refuse:** first-class functions, inheritance, a module system, a standard library, a package
manager. Every one of these is a step down the road Pascal took. The test for a proposed feature is
not "would this be nice" but **"can a textbook algorithm not be written without it?"**

The test bites in both directions, which is the point. `"*" * n` passed (star patterns are the
standard exercise in a loops chapter, and it was three lines of Venos before). Membership and
position in a list passed as a consistency repair — `has` was dictionary-only and `find` was
string-only, so "is it in the list" had no answer. Sorting records by a named field was refused:
the sorting chapter has students write the sort, and everywhere else they can reuse the one they
wrote. Arbitrary-precision integers were refused too — the cost is a different numeric tower, and
the honest answer when the numbers get big is `topython`.

The same judgement came up once inside `topython`. Python reads `xs[-1]` as the last
element, so translating Venos's 1-based index to `xs[i-1]` means that **when the number
drops to 0, an error silently becomes a wrong answer.** Routing every index through a
checking helper closes it, but then insertion sort comes out as `_at(A, j)` instead of
`A[j-1]` — and emitting Python a student can read is the entire point of the feature.
So: **refuse the conversion when the number is written there (`xs[0]`), keep `xs[i-1]`
when it is a variable, and say so in the generated file's header.** The other places
Python answers differently — an empty needle in `replace` and `find`, strings in
`min`/`max`, a 0 start in `substr` — can be closed without costing readability, so they
all are: refused when literal, routed through a checking helper otherwise.
`tests/nopython/` holds those refusals to their wording.

The same judgement came up on performance. Measured against CPython running the same
program via `topython`, Venos is **about 3× slower on loops and lists, 6× on recursion,
and slightly faster on dictionaries** — with one exception: `s = s + ch` in a loop is
**quadratic**, where CPython extends the string in place when it holds the only
reference. A special case in one assignment path would make it linear, and it was
**deliberately not added**: 20,000 characters cost 0.07s, which is invisible at any size
a textbook reaches, and the honest answer when the data gets big is `topython` — the
same answer arbitrary-precision integers got. The measured numbers are in the spec
instead. Measuring and then declining is not the same as never measuring.

## 7. How we would know it is working

Honest, checkable signals, roughly in order of difficulty:

1. Someone who is not the author writes a Venos program and runs `topython` on it.
2. A teacher uses a share link to hand out a starting point.
3. A textbook algorithm — selection sort, binary search — is easier to run in Venos than to set up in
   Python, and someone says so.
4. A classroom uses it for the block→text transition unit.

Downloads and stars are not on this list on purpose.
