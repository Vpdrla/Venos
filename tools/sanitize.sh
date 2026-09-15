#!/usr/bin/env bash
# 새니타이저(ASan + UBSan)로 두 백엔드를 훑는다.
#
#   tools/sanitize.sh              # 코퍼스 + 짧은 퍼징
#   tools/sanitize.sh --fuzz 3     # 퍼징을 3분으로 늘려서
#
# 보통 테스트(tests/run_tests.sh)는 "출력이 맞는가"를 본다.
# 여기서 보는 것은 "메모리를 잘못 건드리거나 정의되지 않은 동작을 하는가"다 —
# 출력이 맞아도 틀릴 수 있는 종류의 버그라서 따로 돈다.
#
# 대상은 두 곳이다. 인터프리터와, 트랜스파일러가 **생성한 C++** 까지.
# 생성 코드의 런타임(RUNTIME 문자열)은 두 번째 구현이라 따로 검사해야 한다.
set -u
cd "$(dirname "$0")/.."

FUZZ_MIN=1
while [ $# -gt 0 ]; do
    case "$1" in
        --fuzz) FUZZ_MIN="$2"; shift 2 ;;
        *) echo "모르는 옵션: $1"; exit 2 ;;
    esac
done

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
SAN_FLAGS="-std=c++17 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer"
# 생성 코드는 36벌을 새로 컴파일해야 해서 최적화 단계가 곧 실행 시간이다.
# 여기서 보는 건 속도가 아니라 메모리 오류라 -O0 으로 충분하다.
GEN_FLAGS="-std=c++17 -g -O0 -fsanitize=address,undefined -fno-omit-frame-pointer"
# 일부러 만든 순환 참조(tests/cases/bugfixes.my, listops_errors.my)가 새는 건 정상이다 —
# 참조 계수 방식에 순환 수집기가 없으면 어쩔 수 없고, 그걸 넣을 언어가 아니다.
export ASAN_OPTIONS=detect_leaks=0
export UBSAN_OPTIONS=print_stacktrace=1

echo "== 새니타이저 빌드"
# shellcheck disable=SC2086
g++ $SAN_FLAGS -o "$TMP/venos-asan" venos.cpp || { echo "빌드 실패"; exit 1; }

bad=0
report() {   # report <이름> <출력>
    if printf '%s' "$2" | grep -qE "runtime error:|ERROR: AddressSanitizer"; then
        echo "FAIL  $1"
        printf '%s' "$2" | grep -E "runtime error:|ERROR: |#1 |#2 " | sort -u | head -6
        bad=$((bad+1))
    fi
}

echo "== 인터프리터로 전체 코퍼스"
for f in tests/cases/*.my examples/algorithms/*.my tests/diag/*.my; do
    name=$(basename "$f" .my)
    inp="tests/cases/$name.input"
    if [ -f "$inp" ]; then out=$("$TMP/venos-asan" "$f" < "$inp" 2>&1)
    else                   out=$("$TMP/venos-asan" "$f" < /dev/null 2>&1); fi
    report "실행/$name" "$out"
done
out=$("$TMP/venos-asan" examples/rpg.my < tests/cases/rpg_path.input 2>&1)
report "실행/rpg" "$out"

echo "== topython"
for f in tests/cases/*.my examples/algorithms/*.my; do
    out=$("$TMP/venos-asan" topython "$f" -o "$TMP/out.py" 2>&1)
    report "topython/$(basename "$f" .my)" "$out"
done

echo "== 생성된 C++ (RUNTIME 문자열 = 두 번째 구현)"
mkdir -p "$TMP/gen"
cp tests/cases/*.my examples/algorithms/*.my "$TMP/gen/" 2>/dev/null
cp tests/cases/*.input "$TMP/gen/" 2>/dev/null
for f in "$TMP/gen"/*.my; do
    name=$(basename "$f" .my)
    "$TMP/venos-asan" build "$f" > /dev/null 2>&1 || { echo "FAIL  빌드/$name"; bad=$((bad+1)); continue; }
    # shellcheck disable=SC2086
    g++ $GEN_FLAGS -o "$TMP/gen/$name.san" "$TMP/gen/$name.cpp" 2>/dev/null \
        || { echo "FAIL  컴파일/$name"; bad=$((bad+1)); continue; }
    if [ -f "$TMP/gen/$name.input" ]; then out=$("$TMP/gen/$name.san" < "$TMP/gen/$name.input" 2>&1)
    else                                   out=$("$TMP/gen/$name.san" < /dev/null 2>&1); fi
    report "생성본/$name" "$out"
done

echo "== 퍼징 (${FUZZ_MIN}분)"
if python3 tools/fuzz.py "$TMP/venos-asan" --rounds 100000 --minutes "$FUZZ_MIN" --out "$TMP/fuzz"; then
    :
else
    # 죽인 입력은 지워지기 전에 꺼내 둔다
    mkdir -p fuzz-crashes && cp "$TMP"/fuzz/crash_* fuzz-crashes/ 2>/dev/null
    echo "죽인 입력을 fuzz-crashes/ 에 남겼습니다."
    bad=$((bad+1))
fi

echo
if [ "$bad" -gt 0 ]; then echo "실패 $bad건"; exit 1; fi
echo "전부 통과"
