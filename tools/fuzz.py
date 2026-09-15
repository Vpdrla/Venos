#!/usr/bin/env python3
"""망가진 소스로 Venos 를 두들겨 본다 — 깔끔하지 않은 죽음을 찾는 퍼저.

    python3 tools/fuzz.py ./venos --rounds 200
    python3 tools/fuzz.py ./venos-asan --minutes 2     # 새니타이저 빌드면 더 잘 잡는다

찾는 것은 출력의 옳고 그름이 아니다. 그건 tests/run_tests.sh 가 본다.
여기서 찾는 것은 **학생이 오타를 냈을 때 언어가 죽는 자리**다:

  - 세그폴트·abort (시그널 사망)
  - 새니타이저 보고 (메모리 오류, UB)
  - 멈춤 (타임아웃)
  - "내부 에러" (우리가 예상 못 한 예외가 새어 나온 것)

문법 에러 자체는 정상이다. `!! 에러: [줄 3] ...` 는 언어가 제대로 일한 것이다.

실제 수확: 재귀 하강 파서에 깊이 제한이 없어 `((((((...` 같은 입력이
세그폴트로 죽던 것을 찾았다 (인터프리터는 128MB 스택 스레드 덕에 버텼지만
topython·build 는 메인 스레드에서 파싱해 5천 단계에 죽었다 — 같은 파일이
실행은 되는데 변환만 죽는 상태였다).

씨앗은 tests/cases 와 examples/algorithms 의 진짜 프로그램이다. 거기서
바이트를 뒤집고, 잘라 내고, 토큰을 끼워 넣고, 괄호를 수천 개 쌓는다.
"""
import argparse
import glob
import hashlib
import os
import random
import subprocess
import sys
import time

# 끼워 넣어 볼 조각들. 초보자가 실제로 흘리는 것(짝 안 맞는 괄호, 따옴표,
# = 와 ==)과 파서가 싫어할 만한 것(한글 한 글자, 역슬래시, 1e999)을 섞었다.
TOKENS = [
    b'{', b'}', b'(', b')', b'[', b']', b'"', b',', b'.', b':', b'=', b'==',
    b'let ', b'if ', b'else ', b'while ', b'for ', b'func ', b'class ', b'return ',
    b'print ', b'self', b'try ', b'catch ', b'import ', b'\n', b'  ', b'-', b'1',
    '가'.encode('utf8'), b'\\', b'{}', b'0x', b'1e999', b'#', b'not ', b'and ',
]


def mutate(src, rng):
    b = bytearray(src) or bytearray(b'x')
    for _ in range(rng.randint(1, 8)):
        i = rng.randrange(len(b)) if b else 0
        op = rng.randint(0, 6)
        if op == 0:                                    # 바이트 뒤집기 (UTF-8 깨짐 포함)
            b[i] ^= 1 << rng.randrange(8)
        elif op == 1:                                  # 잘라내기
            del b[i:i + rng.randint(1, 40)]
        elif op == 2:                                  # 토큰 끼워넣기
            b[i:i] = rng.choice(TOKENS)
        elif op == 3:                                  # 한 조각 복제
            b[i:i] = b[i:min(len(b), i + rng.randint(1, 60))]
        elif op == 4:                                  # 깊게 중첩시키기
            b[i:i] = rng.choice([b'(', b'[', b'{', b'-', b'not ']) * rng.randint(50, 3000)
        elif op == 5:                                  # 줄 하나를 통째로 교체
            lines = bytes(b).split(b'\n')
            lines[rng.randrange(len(lines))] = rng.choice(TOKENS) * rng.randint(1, 5)
            b = bytearray(b'\n'.join(lines))
        else:                                          # 아주 긴 이름/문자열
            b[i:i] = b'a' * rng.randint(1000, 20000)
        if not b:
            b = bytearray(b'x')
    return bytes(b[:200000])


def run(args, stdin, timeout):
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='print_stacktrace=1')
    try:
        p = subprocess.run(args, input=stdin, capture_output=True, timeout=timeout, env=env)
    except subprocess.TimeoutExpired:
        return '멈춤', b''
    out = p.stdout + p.stderr
    if b'runtime error:' in out or b'ERROR: AddressSanitizer' in out:
        return '새니타이저', out
    if p.returncode < 0:
        return '시그널%d' % -p.returncode, out
    if '!! 내부 에러'.encode('utf8') in out:
        return '내부에러', out
    return None, out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('venos', help='검사할 실행 파일 (새니타이저 빌드면 더 잘 잡는다)')
    ap.add_argument('--rounds', type=int, default=200)
    ap.add_argument('--minutes', type=float, default=0.0, help='시간 예산 (0 이면 rounds 만)')
    ap.add_argument('--seed', type=int, default=20260915)
    ap.add_argument('--timeout', type=float, default=10.0, help='한 번 실행의 제한 시간(초)')
    ap.add_argument('--out', default=None, help='죽인 입력을 남길 디렉터리')
    args = ap.parse_args()

    corpus = sorted(glob.glob('tests/cases/*.my') + glob.glob('examples/algorithms/*.my'))
    if not corpus:
        print('씨앗을 못 찾았습니다 — 저장소 최상위에서 실행하세요.', file=sys.stderr)
        return 2
    seeds = [open(p, 'rb').read() for p in corpus]

    out_dir = args.out or os.path.join(os.environ.get('TMPDIR', '/tmp'), 'venos-fuzz')
    os.makedirs(out_dir, exist_ok=True)
    case = os.path.join(out_dir, 'case.my')

    rng = random.Random(args.seed)
    deadline = time.time() + args.minutes * 60 if args.minutes else None
    seen, found, n = set(), 0, 0

    print('씨앗 %d개, 최대 %d회%s' % (
        len(seeds), args.rounds, ', %.1f분 예산' % args.minutes if deadline else ''))

    while n < args.rounds and (deadline is None or time.time() < deadline):
        n += 1
        src = mutate(rng.choice(seeds), rng)
        with open(case, 'wb') as f:
            f.write(src)
        for label, argv in (
            ('실행',   [args.venos, case]),
            ('topython', [args.venos, 'topython', case]),
            ('build',  [args.venos, 'build', case]),
        ):
            kind, out = run(argv, b'1\n2\n3\n' * 50, args.timeout)
            if not kind:
                continue
            # 같은 자리에서 난 것끼리 묶는다 (스택 맨 위 몇 줄로 지문을 만든다)
            top = b'\n'.join(l for l in out.splitlines() if b'#1 ' in l or b'#2 ' in l)
            key = (kind, label, hashlib.sha1(top[:400]).hexdigest()[:8])
            if key in seen:
                continue
            seen.add(key)
            found += 1
            keep = os.path.join(out_dir, 'crash_%s_%s_%d.my' % (kind, label, found))
            with open(keep, 'wb') as f:
                f.write(src)
            print('### %s (%s) -> %s' % (kind, label, keep), flush=True)
            for line in out.decode('utf8', 'replace').splitlines():
                if any(m in line for m in ('runtime error:', 'ERROR:', '#1 ', '#2 ', '#3 ')):
                    print('    ' + line[:200], flush=True)

    print('%d회 실행, 고유 %d종' % (n, len(seen)))
    if seen:
        print('입력은 %s 에 남겼습니다.' % out_dir)
        return 1
    print('깔끔하게 죽지 않은 자리 없음.')
    return 0


sys.exit(main())
