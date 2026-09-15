#!/usr/bin/env bash
# Venos differential 테스트 러너
# 각 tests/cases/*.my 를 세 방식으로 실행해 출력이 일치하는지 비교한다.
#   ① 인터프리터        ② 트랜스파일 빌드본 (C++ → g++)      ③ topython 변환본 (파이썬)
# 알려진 허용 차이는 정규화로 흡수:
#   - 인터프리터 전용 배너 ("=== 실행: ... ===", "=== 정상 종료 ===")
#   - catch 변수 에러 메시지의 "[줄 N]" / "[파일 줄 N]" 접두사 (인터프리터만 포함)
#   - 소수 표기 — Venos 는 C++ 기본(%g, 6자리)으로 보여주고 파이썬은 있는 그대로 보여준다.
#     양쪽 다 %g 로 맞춰서 비교한다 (5.0 → 5, 91.66666666666667 → 91.6667)
set -u
cd "$(dirname "$0")/.."   # 저장소 루트에서 실행

VENOS=./venos
TMP=$(mktemp -d)
cleanup() { rm -rf "$TMP" tests/.tmp_* tests/cases/*.py examples/algorithms/*.py examples/algorithms/*.cpp ; }
trap cleanup EXIT

# 파이썬 변환을 건너뛰는 케이스와 그 이유.
# 전부 "Venos 고유의 에러 문구/런타임 가드"를 출력으로 만드는 케이스다. 파이썬은 같은 상황에서
# 자기 예외 메시지(division by zero, FileNotFoundError, RecursionError ...)를 내므로 문구가 다르다.
# topython 은 "읽을 수 있는 파이썬"을 목표로 하지 에러 문구까지 흉내내지 않는다.
PY_SKIP="errors bugfixes fileio listops"

if [ ! -x "$VENOS" ] || [ venos.cpp -nt "$VENOS" ]; then
    echo "venos 빌드 중..."
    g++ -std=c++17 -O2 -o venos venos.cpp || { echo "빌드 실패"; exit 1; }
fi

PY=$(command -v python3 || true)
[ -n "$PY" ] || echo "(python3 이 없어 파이썬 변환 비교는 건너뜁니다)"

# 배너·줄표시 제거 + 소수를 %g 로 통일
normalize() {
    grep -v '^=== ' "$1" | sed 's/\[[^]]*줄 [0-9]\{1,\}\] //g' | {
        if [ -n "$PY" ]; then
            "$PY" -c 'import re,sys
for line in sys.stdin:
    sys.stdout.write(re.sub(r"-?\d+\.\d+", lambda m: "%g" % float(m.group(0)), line))'
        else
            cat
        fi
    }
}

pass=0; fail=0; pytested=0; pyskipped=0
# tests/cases/*.my 는 언어 기능을, examples/algorithms/*.my 는 교과서 알고리즘을 본다.
# 예제도 스위트에 넣는 이유: "같은 프로그램의 파이썬 버전을 준다"는 약속이 진짜인지는
# 장난감 케이스가 아니라 실제 예제가 세 방식에서 같은 답을 낼 때만 증명된다.
for case_file in tests/cases/*.my examples/algorithms/*.my; do
    [ -e "$case_file" ] || continue
    dir=$(dirname "$case_file")
    name=$(basename "$case_file" .my)
    label="$name"
    [ "$dir" = "tests/cases" ] || label="예제/$name"
    input="$dir/$name.input"
    [ -f "$input" ] || input=/dev/null

    rm -f tests/.tmp_*
    "$VENOS" "$case_file" < "$input" > "$TMP/interp.txt" 2>&1

    # ---- ② C++ 빌드본 ----
    if ! "$VENOS" build "$case_file" > "$TMP/build.txt" 2>&1; then
        echo "FAIL  $label  (빌드 명령 실패)"; cat "$TMP/build.txt"
        fail=$((fail+1)); continue
    fi
    bin="$dir/$name"
    if [ ! -x "$bin" ]; then
        echo "FAIL  $label  (실행 파일이 생성되지 않음)"; cat "$TMP/build.txt"
        fail=$((fail+1)); continue
    fi
    rm -f tests/.tmp_*
    "./$bin" < "$input" > "$TMP/compiled.txt" 2>&1
    rm -f "$bin" "$dir/$name.cpp"

    if ! diff <(normalize "$TMP/interp.txt") <(normalize "$TMP/compiled.txt") > "$TMP/diff.txt" 2>&1; then
        echo "FAIL  $label  (인터프리터/빌드본 출력 불일치)"
        cat "$TMP/diff.txt"
        fail=$((fail+1)); continue
    fi

    # ---- ③ 파이썬 변환본 ----
    skip=no
    for s in $PY_SKIP; do [ "$s" = "$name" ] && skip=yes; done
    if [ -z "$PY" ] || [ "$skip" = yes ]; then
        [ "$skip" = yes ] && pyskipped=$((pyskipped+1))
        echo "PASS  $label"
        pass=$((pass+1)); continue
    fi

    "$VENOS" topython "$case_file" > "$TMP/py.txt" 2>&1
    if [ ! -f "$dir/$name.py" ]; then
        echo "FAIL  $label  (topython 이 .py 를 만들지 못함)"; cat "$TMP/py.txt"
        fail=$((fail+1)); continue
    fi
    rm -f tests/.tmp_*
    "$PY" "$dir/$name.py" < "$input" > "$TMP/python.txt" 2>&1
    rm -f "$dir/$name.py"

    if diff <(normalize "$TMP/interp.txt") <(normalize "$TMP/python.txt") > "$TMP/diff.txt" 2>&1; then
        echo "PASS  $label"
        pass=$((pass+1)); pytested=$((pytested+1))
    else
        echo "FAIL  $label  (인터프리터/파이썬 변환본 출력 불일치)"
        cat "$TMP/diff.txt"
        fail=$((fail+1))
    fi
done

# ---- 에러 메시지 회귀 (tests/diag) ----
# 일부러 틀린 프로그램의 출력이 .expected 와 글자까지 같아야 한다.
# 에러 문구는 초보자가 가장 많이 보는 화면이라 제품 기능으로 취급한다.
dpass=0; dfail=0
for case_file in tests/diag/*.my; do
    [ -e "$case_file" ] || break
    name=$(basename "$case_file" .my)
    want="tests/diag/$name.expected"
    if [ ! -f "$want" ]; then
        echo "FAIL  진단/$name  (.expected 가 없습니다)"
        dfail=$((dfail+1)); continue
    fi
    "$VENOS" "$case_file" > "$TMP/diag.txt" 2>&1
    if diff "$want" "$TMP/diag.txt" > "$TMP/diff.txt" 2>&1; then
        echo "PASS  진단/$name"
        dpass=$((dpass+1))
    else
        echo "FAIL  진단/$name  (에러 메시지가 바뀌었습니다)"
        cat "$TMP/diff.txt"
        dfail=$((dfail+1))
    fi
done

echo
echo "결과: 통과 $pass / 실패 $fail   (파이썬 변환까지 검증 $pytested, 건너뜀 $pyskipped)"
echo "에러 메시지: 통과 $dpass / 실패 $dfail"
[ "$fail" -eq 0 ] && [ "$dfail" -eq 0 ]
