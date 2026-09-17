#!/usr/bin/env python3
"""무작위로 **올바른** Venos 프로그램을 만들어, 세 백엔드의 출력이 같은지 본다.

    python3 tools/genfuzz.py --rounds 200
    python3 tools/genfuzz.py --minutes 3 --keep

`tools/fuzz.py` 와 목적이 다르다. 저쪽은 **망가진** 입력으로 크래시를 찾고,
이쪽은 **돌아가는** 프로그램으로 **의미 차이**를 찾는다 — 인터프리터·C++ 빌드본·
`topython` 이 낸 파이썬이 같은 답을 내는가. 그게 이 언어가 한 약속이다.

손으로 쓴 케이스는 내가 생각해 본 조합만 덮는다. 생성기는 생각 안 해 본 조합을
만든다 — 문자열 곱하기가 리스트 인덱스 안에 들어가고, 중첩된 if 안의 continue 가
try 를 통과하고, 딕셔너리 순회 중에 값을 고치는 식으로.

만들어지는 프로그램은 **에러 없이 끝나도록** 제약한다 (인덱스는 항상 범위 안,
나누는 수는 항상 0 이 아님). 에러 문구는 백엔드마다 다를 수 있어서, 거기까지
비교하려 들면 거짓 실패만 잔뜩 나온다.

딱 한 군데만 예외다. 내장 함수에 **이상한 인자**를 넣어 보는 줄은 try/catch 로 감싸
`"E"` 라는 고정 문구로 바꾼다. 그러면 문구는 안 보면서 **"한쪽은 에러, 다른 쪽은 값"**
이라는 차이만 잡힌다 — num("0x10")·has("abc","b")·sort([[2],[1]]) 가 전부 그 모양이었고,
셋 다 손으로 찔러 보다 나왔다. 손이 찾을 수 있는 종류면 생성기도 찾을 수 있어야 한다.

허용하는 차이는 러너와 같다: 인터프리터 전용 배너, catch 문구의 [줄 N] 접두사,
소수 표기(양쪽을 %g 로 맞춘다).
"""
import argparse
import os
import random
import re
import shutil
import subprocess
import sys
import tempfile
import time

INT_VARS = ['a', 'b', 'c', '수', '값']
STR_VARS = ['s', 't', '글']
LIST_VARS = ['xs', 'ys', '목록']
DICT_VARS = ['d', '딕']
FUNCS = ['더하기', '두배', '큰쪽']
OBJ_VARS = ['상자', '통']

WORDS = ['"가"', '"나"', '"abc"', '"한글"', '"-"', '"x"', '""']

# 내장 함수에 넣어 볼 **이상한 인자**들. "한쪽은 에러, 다른 쪽은 값" 을 찾는 자리라
# 일부러 규칙에서 벗어난 것들을 섞는다 (빈 문자열, 중첩 리스트, 섞인 타입, 0, 음수).
ODD = [
    '0', '-1', '1.5', '"0x10"', '"inf"', '"nan"', '"1_000"', '"1e3"', '"  7  "',
    '""', '"abc"', '"가"', '[]', '[[1], [2]]', '[1, "가"]', '["b", "a"]', '[3, 1, 2]',
    '{"a": 1}', 'xs', 's', 'd', 'a', '상자', '[상자]', '{"o": 상자}',
]
# (이름, 인자 수). 값이 아니라 **에러인가 아닌가**가 셋이 같은지만 본다.
ODD_CALLS = [
    ('num', 1), ('str', 1), ('len', 1), ('sort', 1), ('reverse', 1),
    ('upper', 1), ('lower', 1), ('abs', 1), ('floor', 1), ('ceil', 1), ('sqrt', 1),
    ('has', 2), ('find', 2), ('join', 2), ('split', 2), ('min', 2), ('max', 2),
    ('substr', 3), ('replace', 3),
]


class Gen:
    def __init__(self, rng):
        self.r = rng
        self.depth = 0

    # ---- 식 ----
    def int_expr(self, d=0):
        r = self.r
        if d > 2:
            return r.choice([str(r.randint(-20, 20)), r.choice(INT_VARS)])
        k = r.randint(0, 9)
        if k <= 2:
            return str(r.randint(-20, 20))
        if k == 3:
            return r.choice(INT_VARS)
        if k == 4:
            return 'len(%s)' % r.choice(LIST_VARS + STR_VARS)
        if k == 5:                                   # 0 으로 나누지 않게 나누는 수는 리터럴
            return '(%s %% %d)' % (self.int_expr(d + 1), r.choice([2, 3, 5, 7]))
        if k == 6:
            return '%s(%s, %s)' % (r.choice(['min', 'max']), self.int_expr(d + 1), self.int_expr(d + 1))
        if k == 7:
            return 'abs(%s)' % self.int_expr(d + 1)
        if k == 8:
            return '%s(%s, %s)' % (r.choice(FUNCS), self.int_expr(d + 1), self.int_expr(d + 1))
        if k == 9 and d == 0:                        # 객체의 필드와 메서드
            o = r.choice(OBJ_VARS)
            return r.choice(['%s.값' % o, '%s.더하기(%s)' % (o, self.int_expr(d + 1)),
                             '%s.센것()' % o])
        op = r.choice(['+', '-', '*'])
        both = (self.int_expr(d + 1), self.int_expr(d + 1))
        # 곱은 금방 2^53 을 넘긴다. 넘어가면 Venos(실수 하나)는 1.67445e+26 으로,
        # 파이썬(무한 정수)은 167445455488729388610101297 로 찍는다 — **명세에 적힌 차이**지
        # 버그가 아니다. 여기서 잡히면 거짓 실패다. 그래서 곱한 값은 범위 안으로 접는다.
        # (이 %% 를 지우면 CI 가 씨앗에 따라 가끔 빨개진다. 실제로 그랬다.)
        if op == '*':
            return '((%s * %s) %% 100003)' % both
        return '(%s %s %s)' % (both[0], op, both[1])

    def str_expr(self, d=0):
        r = self.r
        if d > 2:
            return r.choice(WORDS + STR_VARS)
        k = r.randint(0, 7)
        if k <= 1:
            return r.choice(WORDS)
        if k == 2:
            return r.choice(STR_VARS)
        if k == 3:
            return '(%s + %s)' % (self.str_expr(d + 1), self.str_expr(d + 1))
        if k == 4:                                   # 반복 횟수는 음수가 되지 않게
            return '(%s * (abs(%s) %% 4))' % (self.str_expr(d + 1), self.int_expr(d + 1))
        if k == 5:
            return '%s(%s)' % (r.choice(['upper', 'lower', 'reverse']), self.str_expr(d + 1))
        if k == 6:
            return 'str(%s)' % self.int_expr(d + 1)
        if k == 7 and d <= 1:
            return self.interp(d)
        return 'join(%s, %s)' % (r.choice(LIST_VARS), r.choice(['"-"', '","', '""']))

    # 문자열 보간 — PyGen 이 f-string 으로 되돌리는 자리다. 역사적으로 버그가 제일 많았다
    # (파이썬 표기 누출, 따옴표 충돌, 하위 파서의 줄 번호).
    def interp(self, d=0):
        r = self.r
        bits = []
        for _ in range(r.randint(1, 3)):
            k = r.randint(0, 5)
            if k == 0:
                bits.append('{%s}' % r.choice(INT_VARS))
            elif k == 1:
                bits.append('{%s}' % self.int_expr(d + 2))
            elif k == 2:
                bits.append('{%s}' % r.choice(STR_VARS))
            elif k == 3:
                bits.append('{len(%s)}' % r.choice(LIST_VARS))
            elif k == 4:                             # 보간 안의 따옴표는 이스케이프해야 한다
                bits.append('{join(%s, \\"%s\\")}' % (r.choice(LIST_VARS), r.choice(['-', ',', ' '])))
            else:
                bits.append(r.choice(['값:', '-', '가', ' ', '끝']))
        return '"%s"' % ''.join(bits)

    def index(self, container):
        # 항상 범위 안 (1부터). 빈 리스트는 만들지 않는다.
        return '((abs(%s) %% len(%s)) + 1)' % (self.int_expr(2), container)

    def any_expr(self, d=0):
        return self.str_expr(d) if self.r.random() < 0.4 else self.int_expr(d)

    def cond(self, d=0):
        r = self.r
        a = '%s %s %s' % (self.int_expr(d + 1), r.choice(['<', '>', '<=', '>=', '==', '!=']),
                          self.int_expr(d + 1))
        k = r.randint(0, 4)
        if k == 0:
            return 'not (%s)' % a
        if k == 1:
            return '(%s) %s (%s)' % (a, r.choice(['and', 'or']),
                                     '%s == %s' % (self.str_expr(d + 1), self.str_expr(d + 1)))
        return a

    # ---- 문장 ----
    # 내장 함수에 이상한 인자를 넣어 보는 한 줄. try/catch 로 감싸 **문구가 아니라
    # "에러인가 값인가"** 만 비교되게 만든다 (값이 나오면 그 값도 같아야 한다).
    def odd_call(self, pad):
        r = self.r
        name, n = r.choice(ODD_CALLS)
        args = ', '.join(r.choice(ODD) for _ in range(n))
        return ['%stry { print %s(%s) } catch 오류 { print "E" }' % (pad, name, args)]

    def stmt(self, ind, d=0):
        r = self.r
        pad = '    ' * ind
        k = r.randint(0, 13)
        if k == 13 and d == 0:
            return self.odd_call(pad)
        if d > 2:
            k = r.choice([0, 1, 2, 3])
        if k == 0:
            return ['%s%s = %s' % (pad, r.choice(INT_VARS), self.int_expr())]
        if k == 1:
            return ['%s%s = %s' % (pad, r.choice(STR_VARS), self.str_expr())]
        if k == 2:
            return ['%sprint %s' % (pad, ', '.join(self.any_expr() for _ in range(r.randint(1, 3))))]
        if k == 3:
            c = r.choice(LIST_VARS)
            return ['%spush(%s, %s)' % (pad, c, self.any_expr())]
        if k == 12 and d == 0:
            c = r.choice(LIST_VARS)
            return [r.choice(['%sprint %s' % (pad, self.interp()),
                              '%sprint copy(%s)' % (pad, c),
                              '%sprint [%s, [%s, %s]]' % (pad, self.int_expr(1), self.str_expr(1), self.int_expr(1)),
                              '%s%s = copy(%s)' % (pad, r.choice(LIST_VARS), c)])]
        if k == 4:
            c = r.choice(LIST_VARS)
            return ['%s%s[%s] = %s' % (pad, c, self.index(c), self.any_expr())]
        if k == 5:
            c = r.choice(DICT_VARS)
            return ['%s%s[%s] = %s' % (pad, c, r.choice(['"k1"', '"k2"', '"키"']), self.any_expr())]
        if k == 6:
            out = ['%sif %s {' % (pad, self.cond())] + self.body(ind + 1, d + 1)
            if r.random() < 0.5:
                out += ['%s} else {' % pad] + self.body(ind + 1, d + 1)
            return out + ['%s}' % pad]
        if k == 7:
            v = r.choice(['i', 'j', '번'])
            lo, hi = r.randint(1, 3), r.randint(1, 5)
            step = r.choice(['', ' step 2', ' step -1', ''])
            if step == ' step -1':
                lo, hi = hi, 1
            elif not step:
                # step 이 없으면 for 는 항상 올라가고, 양끝이 상수인데 거꾸로면
                # 파서가 거절한다 — 그건 잘못된 프로그램이므로 만들지 않는다.
                lo, hi = min(lo, hi), max(lo, hi)
                if r.random() < 0.4:
                    # 대신 끝값을 식으로 줘서 **한 번도 안 도는** 경우를 만든다.
                    # 세 방식이 다 0번 돌아야 하는 자리다.
                    return (['%sfor %s = %d to %s - %d {'
                             % (pad, v, lo, r.choice(INT_VARS), r.randint(0, 6))]
                            + self.body(ind + 1, d + 1, loop=True) + ['%s}' % pad])
            return (['%sfor %s = %d to %d%s {' % (pad, v, lo, hi, step)]
                    + self.body(ind + 1, d + 1, loop=True) + ['%s}' % pad])
        if k == 8:
            c = r.choice(LIST_VARS + DICT_VARS)
            v = r.choice(['x', 'e', '항목'])
            return (['%sfor %s in %s {' % (pad, v, c)]
                    + self.body(ind + 1, d + 1, loop=True) + ['%s}' % pad])
        if k == 9:                                   # 반드시 끝나는 while
            v = r.choice(['n', 'm'])
            return (['%slet %s = %d' % (pad, v, r.randint(1, 4)),
                     '%swhile %s > 0 {' % (pad, v)]
                    + self.body(ind + 1, d + 1, loop=True)
                    + ['%s    %s -= 1' % (pad, v), '%s}' % pad])
        if k == 10:
            return (['%stry {' % pad] + self.body(ind + 1, d + 1)
                    + ['%s} catch 오류 {' % pad, '%s    print "잡음"' % pad, '%s}' % pad])
        if k == 11 and d == 0:
            o = r.choice(OBJ_VARS)
            return [r.choice(['%s%s.값 = %s' % (pad, o, self.int_expr()),
                              '%s%s.값 += %s' % (pad, o, self.int_expr()),
                              '%s%s.담기(%s)' % (pad, o, self.any_expr()),
                              '%sprint %s.값, %s.센것()' % (pad, o, o)])]
        c = r.choice(LIST_VARS)
        return ['%sprint %s[%s]' % (pad, c, self.index(c))]

    def body(self, ind, d, loop=False):
        out = []
        for _ in range(self.r.randint(1, 3)):
            out += self.stmt(ind, d)
        if loop and self.r.random() < 0.25:
            pad = '    ' * ind
            out.append('%sif %s { %s }' % (pad, self.cond(), self.r.choice(['break', 'continue'])))
        return out or ['%sprint "빈 블록"' % ('    ' * ind)]

    def program(self):
        r = self.r
        L = ['# genfuzz 가 만든 프로그램 (seed 로 재현)']
        L.append('func 더하기(p, q) { return p + q }')
        L.append('func 두배(p, q) { return (p + q) * 2 }')
        L.append('func 큰쪽(p, q) { if p > q { return p }  return q }')
        L.append('class 그릇 {')
        L.append('    func init(시작) { self.값 = 시작  self.담은것 = [] }')
        L.append('    func 담기(v) { push(self.담은것, v)  self.값 += 1  return self }')
        L.append('    func 센것() { return len(self.담은것) }')
        L.append('    func 더하기(n) { return self.값 + n }')
        L.append('}')
        for v in INT_VARS:
            L.append('let %s = %d' % (v, r.randint(-9, 9)))
        for v in STR_VARS:
            L.append('let %s = %s' % (v, r.choice(WORDS)))
        for v in LIST_VARS:                          # 비지 않게 (인덱스 계산이 len 을 쓴다)
            L.append('let %s = [%s]' % (v, ', '.join(str(r.randint(0, 9)) for _ in range(r.randint(2, 4)))))
        for v in DICT_VARS:
            L.append('let %s = {"k1": %d}' % (v, r.randint(0, 9)))
        for v in OBJ_VARS:
            L.append('let %s = 그릇(%d)' % (v, r.randint(0, 5)))
        for _ in range(r.randint(4, 10)):
            L += self.stmt(0)
        L.append('print "--- 끝 ---"')
        for v in INT_VARS + STR_VARS + LIST_VARS:
            L.append('print "%s =", %s' % (v, v))
        for v in DICT_VARS:
            L.append('print "%s =", keys(%s)' % (v, v))
        for v in OBJ_VARS:
            L.append('print "%s =", %s.값, %s.센것(), %s.담은것' % (v, v, v, v))
        return '\n'.join(L) + '\n'


# ---- 백엔드 실행 ----
def norm(text):
    """러너와 같은 정규화 — 허용된 차이만 흡수한다."""
    out = []
    for line in text.splitlines():
        if (line.startswith('=== running:') or line == '=== done ==='
                or line.startswith('=== build:') or line.startswith('C++ generated:')
                or line.startswith('Python generated:') or line.startswith('compiling with')
                or line.startswith('build OK:') or line == '----- run -----'
                or line.startswith('(note: non-ASCII')):
            continue
        line = re.sub(r'\[(?:[^\]]*줄 \d+)\]\s*', '', line)      # catch 메시지의 [줄 N]
        # 소수 표기를 %g 로 맞춘다 (파이썬 91.66666666666667 vs Venos 91.6667)
        line = re.sub(r'-?\d+\.\d+', lambda m: '%g' % float(m.group(0)), line)
        out.append(line.rstrip())
    return '\n'.join(out).strip()


def run(cmd, cwd, timeout=30):
    try:
        p = subprocess.run(cmd, cwd=cwd, capture_output=True, timeout=timeout, text=True,
                           errors='replace')
        return p.stdout + p.stderr
    except subprocess.TimeoutExpired:
        return '<<타임아웃>>'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--venos', default='./venos')
    ap.add_argument('--rounds', type=int, default=100)
    ap.add_argument('--minutes', type=float, default=0.0)
    ap.add_argument('--seed', type=int, default=20260915)
    ap.add_argument('--keep', action='store_true', help='어긋난 프로그램을 genfuzz-diffs/ 에 남긴다')
    args = ap.parse_args()

    venos = os.path.abspath(args.venos)
    if not os.path.exists(venos):
        print('venos 실행 파일이 없습니다: %s' % venos, file=sys.stderr)
        return 2

    rng = random.Random(args.seed)
    deadline = time.time() + args.minutes * 60 if args.minutes else None
    tmp = tempfile.mkdtemp(prefix='venos-genfuzz-')
    bad = n = skipped = refused = 0
    print('무작위 프로그램으로 3중 비교 (seed %d)' % args.seed)
    try:
        while n < args.rounds and (deadline is None or time.time() < deadline):
            n += 1
            src = Gen(rng).program()
            path = os.path.join(tmp, 'case.my')
            open(path, 'w', encoding='utf8').write(src)
            for f in ('case.py', 'case.cpp', 'case'):
                if os.path.exists(os.path.join(tmp, f)):
                    os.remove(os.path.join(tmp, f))

            interp = norm(run([venos, 'case.my'], tmp))
            if '!! ' in interp or '<<타임아웃>>' in interp:
                skipped += 1                          # 에러로 끝난 프로그램은 비교 대상이 아니다
                continue

            built = norm(run([venos, 'build', 'case.my', 'run'], tmp, 120))
            conv = run([venos, 'topython', 'case.my'], tmp)
            # **거절은 틀린 답이 아니다.** topython 은 파이썬이 다르게 답하는 자리를 일부러
            # 줄 번호와 함께 거절한다 (min("a","b") 처럼). 생성기가 그런 줄을 만들면 여기로
            # 오는데, 그걸 어긋남으로 세면 설계대로 동작한 것을 실패로 읽는 꼴이다.
            if 'python conversion failed' in conv:
                refused += 1
                continue
            pyout = norm(run([sys.executable, 'case.py'], tmp)) if 'Python generated' in conv else '<<변환 실패>>'

            for label, got in (('빌드본', built), ('파이썬', pyout)):
                if got != interp:
                    bad += 1
                    print('\n### %s 가 인터프리터와 다름 (%d회차)' % (label, n))
                    a, b = interp.splitlines(), got.splitlines()
                    for i in range(max(len(a), len(b))):
                        x = a[i] if i < len(a) else '<없음>'
                        y = b[i] if i < len(b) else '<없음>'
                        if x != y:
                            print('  줄 %d  인터프리터: %s' % (i + 1, x[:100]))
                            print('        %s: %s' % (label, y[:100]))
                            break
                    if args.keep:
                        os.makedirs('genfuzz-diffs', exist_ok=True)
                        dst = os.path.join('genfuzz-diffs', 'diff_%d_%s.my' % (n, label))
                        shutil.copy(path, dst)
                        print('  입력: %s' % dst)
                    break
            if n % 25 == 0:
                print('... %d회 (건너뜀 %d, 거절 %d, 어긋남 %d)' % (n, skipped, refused, bad),
                      file=sys.stderr, flush=True)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    print('\n%d회 생성 · %d회 비교 (에러로 끝나 건너뜀 %d, topython 이 일부러 거절 %d)'
          ' · 어긋남 %d건' % (n, n - skipped - refused, skipped, refused, bad))
    return 1 if bad else 0


sys.exit(main())
