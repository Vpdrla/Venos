# Textbook algorithms, running

*English | [한국어](README.ko.md)*

Fifteen algorithms from the standard informatics curriculum, each written to sit
line-for-line against the pseudocode a textbook prints. Every file starts with that
pseudocode in a comment, so you can read the two side by side.

This folder is also the honest test of what Venos claims to be. Every file here is
run three ways by `tests/run_tests.sh` — the interpreter, the transpiled C++ binary,
and the Python that `venos topython` emits — and all three must print the same thing,
character for character. Not toy cases: these programs.

## Running one

```bash
venos selection-sort.my              # run it
venos topython selection-sort.my     # write selection-sort.py, the same program in Python
venos build selection-sort.my run    # compile to a native binary and run that
```

Or paste a file into the [playground](https://vpdrla.github.io/Venos/) — nothing to
install — and press **🐍 Python** to see the Python version.

## What's here

| File | Algorithm | What it shows |
|---|---|---|
| `selection-sort.my` | Selection sort | Comparisons are `n(n-1)/2` whatever the data |
| `insertion-sort.my` | Insertion sort | Moves depend on the data — nearly-sorted input is nearly free |
| `bubble-sort.my` | Bubble sort | Stopping early when a pass swaps nothing |
| `linear-search.my` | Linear search | Best 1, worst n, average (n+1)/2 comparisons |
| `binary-search.my` | Binary search | The range halving, step by step; 1,000,000 items in 20 comparisons |
| `gcd.my` | Euclid's algorithm | Iterative and recursive, plus LCM; contrasted with trying every divisor |
| `primes.my` | Primality, sieve of Eratosthenes | Prime factorization; how primes thin out |
| `fibonacci.my` | Fibonacci, three ways | Naive recursion vs. iteration vs. memoization, with the call counts that explain why |
| `hanoi.my` | Towers of Hanoi | Recursion that is genuinely hard to write any other way |
| `base-convert.my` | Base conversion | Decimal ↔ binary/octal/hex, both directions |
| `palindrome.my` | Reversal, palindromes | Two-pointer and reverse-and-compare; longest palindromic substring |
| `statistics.my` | Mean, median, mode, σ | With the grade distribution as a bar chart |
| `matrix.my` | Matrix add/multiply/transpose | Why `A×B ≠ B×A` |
| `stack-queue.my` | Stack and queue | Built from lists; balanced parentheses, a print queue |
| `caesar-cipher.my` | Caesar cipher | Brute force and letter frequency; needs `%` to wrap negatives |

## One thing to know before reading

**List indices start at 1**, matching the pseudocode in textbooks (`A[1]` is the first
element). That is the one place where translating to Python changes something, and
`topython` handles it for you — it writes `A[i-1]` and says so in the generated file's
header.

## Two limits worth knowing

`hanoi.my` counts moves for 50 discs, not the legendary 64: Venos numbers are
doubles, exact up to about 9 quadrillion, and 2^64−1 is past that. Python's integers
have no such limit, so `venos topython` is a real answer when the numbers get big.

None of these files use `random()` or `time()`, because the differential suite needs
the same output every run.
