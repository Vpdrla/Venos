#!/usr/bin/env bash
# Venos differential 테스트 러너
# 각 tests/cases/*.my 를 세 방식으로 실행해 출력이 일치하는지 비교한다.
#   ① 인터프리터        ② 트랜스파일 빌드본 (C++ → g++)      ③ topython 변환본 (파이썬)
# 알려진 허용 차이는 정규화로 흡수:
#   - 인터프리터 전용 배너 ("=== 실행: ... ===", "=== 정상 종료 ===")
#   - 인터프리터 전용 호출 경로 ("부른 순서: ...") — 빌드본은 줄번호 자체를 안 들고 있다
#   - catch 변수 에러 메시지의 "[줄 N]" / "[파일 줄 N]" 접두사 (인터프리터만 포함)
# 소수 표기는 **정규화하지 않는다**. 예전엔 양쪽을 %g 로 맞춰서 비교했는데, PyGen 의
# _show 가 Venos 의 표기 규칙(|x|<9e18 이고 정수면 자릿수를 다 쓰고 그 위는 %g)을
# 그대로 따르게 된 뒤로는 53개 케이스가 정규화 없이도 글자까지 같다. 그대로 두면
# **앞으로 생길 진짜 표기 차이를 가려 준다** — 가리는 그물은 없는 그물보다 나쁘다.
set -u
cd "$(dirname "$0")/.."   # 저장소 루트에서 실행

# Windows 에서는 g++ 가 확장자 없는 -o 에 .exe 를 붙인다 — 양쪽 이름을 다 받아 준다
VENOS=./venos
[ -x "$VENOS" ] || [ ! -x ./venos.exe ] || VENOS=./venos.exe
EXE=""
case "$(uname -s 2>/dev/null)" in MINGW*|MSYS*|CYGWIN*) EXE=".exe" ;; esac
TMP=$(mktemp -d)
cleanup() { rm -rf "$TMP" tests/diag/*.py tests/diag/*.cpp tests/diag/*.exe tests/.tmp_* tests/cases/*.py tests/cases/*.exe examples/algorithms/*.py examples/algorithms/*.cpp examples/algorithms/*.exe examples/rpg.py examples/rpg.cpp examples/rpg examples/rpg.exe save.txt ; }
trap cleanup EXIT

# 파이썬 변환을 건너뛰는 케이스와 그 이유.
# 전부 "Venos 고유의 에러 문구/런타임 가드"를 출력으로 만드는 케이스다. 파이썬은 같은 상황에서
# 자기 예외 메시지(division by zero, FileNotFoundError, RecursionError ...)를 내므로 문구가 다르다.
# topython 은 "읽을 수 있는 파이썬"을 목표로 하지 에러 문구까지 흉내내지 않는다.
# cycles 는 성격이 조금 다르다: 파이썬의 deepcopy 는 memo 로 순환을 **처리해 버리고**,
# repr 은 [...] 로 접는다. Venos 는 둘 다 거절한다 — 구조적 차이라 문구만의 문제가 아니다.
# 여기 빠지려면 리스트를 자기 안에 일부러 push 해야 하므로 리스트 * 2 와 같은 거래로 뒀다.
PY_SKIP="errors bugfixes fileio_errors listops_errors rpg error_wording cycles"

# random() 을 쓰는 프로그램은 씨앗을 고정해야 백엔드끼리 비교할 수 있다.
# 언어에 seed() 를 더하지 않고 **환경변수**로 둔 이유는 STRATEGY §6 에 적어 뒀다 —
# 학생이 배울 표면은 늘지 않고, 러너만 쓴다. 파이썬은 난수 구현이 달라 못 낀다
# (그래서 rpg 가 PY_SKIP 에 있다). 이걸 지우면 rpg 가 매번 다른 출력을 낸다.
export VENOS_SEED=20260917

if [ ! -x "$VENOS" ] || [ venos.cpp -nt "$VENOS" ]; then
    echo "venos 빌드 중..."
    g++ -std=c++17 -O2 -o "venos$EXE" venos.cpp || { echo "빌드 실패"; exit 1; }
    VENOS="./venos$EXE"
fi

PY=$(command -v python3 || true)
[ -n "$PY" ] || echo "(python3 이 없어 파이썬 변환 비교는 건너뜁니다)"

# 배너·호출 경로·[줄 N] 제거. 소수는 손대지 않는다 (위 설명 참고).
normalize() {
    # 윈도우는 리디렉션된 stdout 에 CRLF 를 쓴다 — 비교 전에 걷어낸다
    tr -d '\r' < "$1" | grep -v '^=== ' | grep -v '^    부른 순서: ' | sed 's/\[[^]]*줄 [0-9]\{1,\}\] //g' 
}

pass=0; fail=0; pytested=0; pyskipped=0
failed_names=""
note_fail() { failed_names="$failed_names $1"; fail=$((fail+1)); }
# tests/cases/*.my 는 언어 기능을, examples/algorithms/*.my 는 교과서 알고리즘을 본다.
# 예제도 스위트에 넣는 이유: "같은 프로그램의 파이썬 버전을 준다"는 약속이 진짜인지는
# 장난감 케이스가 아니라 실제 예제가 세 방식에서 같은 답을 낼 때만 증명된다.
# examples/rpg.my 는 이 언어로 쓴 제일 큰 프로그램(222줄)이고, random 을 쓴다는 이유로
# 오래 스위트 밖에 있었다 — 클래스·상속 없는 다형성·문자열 보간·파일 저장이 한꺼번에
# 도는 유일한 자리인데도. VENOS_SEED 로 수열을 고정하니 들어올 수 있게 됐다.
for case_file in tests/cases/*.my examples/algorithms/*.my examples/rpg.my; do
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
        note_fail "$label"; continue
    fi
    bin="$dir/$name$EXE"
    if [ ! -x "$bin" ]; then
        echo "FAIL  $label  (실행 파일이 생성되지 않음)"; cat "$TMP/build.txt"
        note_fail "$label"; continue
    fi
    rm -f tests/.tmp_*
    "./$bin" < "$input" > "$TMP/compiled.txt" 2>&1
    rm -f "$bin" "$dir/$name.cpp"

    if ! diff <(normalize "$TMP/interp.txt") <(normalize "$TMP/compiled.txt") > "$TMP/diff.txt" 2>&1; then
        echo "FAIL  $label  (인터프리터/빌드본 출력 불일치)"
        cat "$TMP/diff.txt"
        note_fail "$label"; continue
    fi

    # ---- ③ 파이썬 변환본 ----
    skip=no
    for s in $PY_SKIP; do [ "$s" = "$name" ] && skip=yes; done
    if [ -z "$PY" ] || [ "$skip" = yes ]; then
        # PY_SKIP 은 **출력 비교**를 건너뛰는 것이지 파이썬을 안 보는 게 아니다.
        # 변환본을 돌려서, 코드젠이 틀렸을 때만 나올 수 있는 예외가 있는지는 본다
        # (NameError = 안 만든 이름, AttributeError = 없는 메서드, SyntaxError = 깨진 파일).
        # 우리 도우미가 일부러 내는 Exception 은 여기 안 걸린다.
        if [ -n "$PY" ] && [ "$skip" = yes ]; then
            pyskipped=$((pyskipped+1))
            # 이 케이스들은 Venos 고유 동작에 기대므로 topython 이 **일부러 거절**할 수도
            # 있다 (errors.my 의 xs[0] 이 그렇다). 거절은 정상이고, .py 가 나왔을 때만 본다.
            "$VENOS" topython "$case_file" > "$TMP/py.txt" 2>&1
            if [ ! -f "$dir/$name.py" ]; then
                echo "PASS  $label"
                pass=$((pass+1)); continue
            fi
            rm -f tests/.tmp_*
            "$PY" "$dir/$name.py" < "$input" > "$TMP/python.txt" 2>&1
            rm -f "$dir/$name.py"
            if grep -qE 'NameError|AttributeError|SyntaxError|IndentationError|UnboundLocalError|ImportError' "$TMP/python.txt"; then
                echo "FAIL  $label  (생성 파이썬이 코드젠 오류로 죽음)"
                grep -nE 'NameError|AttributeError|SyntaxError|IndentationError|UnboundLocalError|ImportError' "$TMP/python.txt" | head -3
                note_fail "$label"; continue
            fi
        elif [ "$skip" = yes ]; then
            pyskipped=$((pyskipped+1))
        fi
        echo "PASS  $label"
        pass=$((pass+1)); continue
    fi

    "$VENOS" topython "$case_file" > "$TMP/py.txt" 2>&1
    if [ ! -f "$dir/$name.py" ]; then
        echo "FAIL  $label  (topython 이 .py 를 만들지 못함)"; cat "$TMP/py.txt"
        note_fail "$label"; continue
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
        note_fail "$label"
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
    if ! diff <(tr -d '\r' < "$want") <(tr -d '\r' < "$TMP/diag.txt") > "$TMP/diff.txt" 2>&1; then
        echo "FAIL  진단/$name  (에러 메시지가 바뀌었습니다)"
        cat "$TMP/diff.txt"
        dfail=$((dfail+1)); continue
    fi
    # 이 프로그램들은 **인터프리터가 거절하는** 것들이다. 다른 백엔드가 통과시키고
    # 멀쩡히 답을 내면, 같은 파일이 백엔드마다 다른 프로그램이 된다.
    # ("abc".upper() 가 여기서는 에러, 파이썬에서는 ABC 였다 — 이 검사가 잡았다.
    #  `let g = 계산` 도 같은 모양이었다. 검사가 백엔드 하나에만 있는 실수는 조용하다.)
    # 먼저 C++ 빌드본. 대부분 파싱에서 거절되므로 g++ 까지 가는 건 몇 개뿐이다.
    rm -f "tests/diag/$name" "tests/diag/$name.exe" "tests/diag/$name.cpp"
    if "$VENOS" build "tests/diag/$name.my" > "$TMP/dbuild.txt" 2>&1 \
       && [ -x "tests/diag/$name$EXE" ]; then
        "./tests/diag/$name$EXE" > "$TMP/dbrun.txt" 2>&1 < /dev/null
        if ! grep -qE '에러|Error' "$TMP/dbrun.txt"; then
            echo "FAIL  진단/$name  (빌드본이 통과시켰고 에러 없이 끝났습니다)"
            head -3 "$TMP/dbrun.txt"
            dfail=$((dfail+1)); rm -f "tests/diag/$name$EXE" "tests/diag/$name.cpp"; continue
        fi
    fi
    rm -f "tests/diag/$name$EXE" "tests/diag/$name.cpp"
    if [ -n "$PY" ]; then
        rm -f "tests/diag/$name.py"
        if "$VENOS" topython "$case_file" > "$TMP/dpy.txt" 2>&1 && [ -f "tests/diag/$name.py" ]; then
            "$PY" "tests/diag/$name.py" > "$TMP/dpyrun.txt" 2>&1
            rm -f "tests/diag/$name.py"
            if ! grep -qE 'Traceback|Error' "$TMP/dpyrun.txt"; then
                echo "FAIL  진단/$name  (topython 이 통과시켰고 파이썬이 답을 냈습니다)"
                head -3 "$TMP/dpyrun.txt"
                dfail=$((dfail+1)); continue
            fi
        fi
    fi
    echo "PASS  진단/$name"
    dpass=$((dpass+1))
done

# ---- 셸 편집 모드 (create / code / :d / :q / run) ----
# 에디터가 없는 학생이 코드를 치는 유일한 자리인데 아무 테스트도 없었다.
# 화면 모양은 보지 않는다 (사소한 문구 변경에 깨지므로) — 파일이 제대로 저장되고
# 셸에서 실행·변환이 되는지만 본다.
shell_ok="통과"
VENOS_ABS=$(cd "$(dirname "$VENOS")" && pwd)/$(basename "$VENOS")
# 서브셸 — 셸이 파일을 현재 폴더에 만들기 때문에 옮겨 가야 하는데,
# 여기서 cd 가 새면 뒤 단계들이 조용히 엉뚱한 곳에서 돈다.
(
    mkdir -p "$TMP/shell" && cd "$TMP/shell" || exit 0
    printf 'create 셸테스트\ncode\nlet x = 5\nprint "두 배:", x * 2\nprint "이 줄은 지운다"\n:d\nprint "끝"\n:q\nrun\ntopython\nexit\n' \
        | "$VENOS_ABS" > out.txt 2>&1
) || true
if ! grep -q '^print "끝"$' "$TMP/shell/셸테스트.my" 2>/dev/null \
   || grep -q '이 줄은 지운다' "$TMP/shell/셸테스트.my" 2>/dev/null; then
    shell_ok="실패 (저장된 파일이 틀립니다)"
    cat "$TMP/shell/셸테스트.my" 2>/dev/null
    dfail=$((dfail+1))
elif ! grep -q '두 배: 10' "$TMP/shell/out.txt" 2>/dev/null; then
    shell_ok="실패 (셸의 run 이 안 됩니다)"
    tail -20 "$TMP/shell/out.txt" 2>/dev/null
    dfail=$((dfail+1))
elif [ ! -f "$TMP/shell/셸테스트.py" ]; then
    shell_ok="실패 (셸의 topython 이 .py 를 안 만들었습니다)"
    dfail=$((dfail+1))
fi

# ---- topython 이 거절해야 하는 것들 (tests/nopython) ----
# 틀린 파이썬을 내는 건 거절보다 나쁘다 — 학생은 틀린 줄 알 길이 없다.
# 파이썬이 Venos 와 다르게 답하는 자리에서 줄 번호를 대고 거절하는지 본다.
npass=0
for case_file in tests/nopython/*.my; do
    [ -e "$case_file" ] || break
    name=$(basename "$case_file" .my)
    want="tests/nopython/$name.expected"
    if [ ! -f "$want" ]; then
        echo "FAIL  거절/$name  (.expected 가 없습니다)"; dfail=$((dfail+1)); continue
    fi
    "$VENOS" topython "$case_file" > "$TMP/nopy.txt" 2>&1
    rm -f "tests/nopython/$name.py"
    if diff <(tr -d '\r' < "$want") <(tr -d '\r' < "$TMP/nopy.txt") > "$TMP/diff.txt" 2>&1; then
        echo "PASS  거절/$name"; npass=$((npass+1))
    else
        echo "FAIL  거절/$name  (거절 문구가 바뀌었거나, 거절하지 않았습니다)"
        cat "$TMP/diff.txt"; dfail=$((dfail+1))
    fi
done

# ---- REPL ----
# 셸의 `repl` 은 스위트 어디에도 없었다. 값이 바로 찍히는지, **에러 뒤에도 이어지는지**,
# 나갈 수 있는지만 본다 — 화면 문구는 사소한 변경에 깨지므로 보지 않는다.
repl_ok="통과"
# 여러 줄 입력도 본다: 블록(`{`)뿐 아니라 **리스트·딕셔너리·괄호**도 이어져야 한다.
# `{}` 만 세던 때는 스펙이 권하는 여러 줄 리스트가 REPL 에서만 죽었다.
printf 'repl\nlet x = 3\nx * 7\n"안녕" + str(x)\nlet xs = [1, 2, 3]\nlen(xs)\n없는변수\nx + 1\nfor i = 1 to 2 { print i }\nxs\nlet 여러줄 = [7,\n8,\n9]\n여러줄\nlet 표 = {\n"k": 42\n}\n표["k"]\n:q\nexit\n' \
    | "$VENOS" > "$TMP/repl.txt" 2>&1
for want in '21' '안녕3' '정의되지 않은 변수' '\[1, 2, 3\]' '\[7, 8, 9\]' '42'; do
    grep -q -- "$want" "$TMP/repl.txt" || { repl_ok="실패 ($want 없음)"; dfail=$((dfail+1)); }
done
# 에러 뒤에도 이어지는가 — 다음 줄의 x + 1 이 4 를 내야 한다 (여기 말고 4 가 나올 데는 없다)
grep -qE '(^|[^0-9])4([^0-9]|$)' "$TMP/repl.txt" \
    || { repl_ok="실패 (에러 뒤에 이어지지 않습니다)"; dfail=$((dfail+1)); }
[ "$repl_ok" = "통과" ] || cat "$TMP/repl.txt"

# ---- 종료 코드 ----
# 실패를 0 으로 알리면 채점 스크립트와 Makefile 이 죽은 프로그램을 성공으로 읽는다.
# 인터프리터가 오래 0 만 돌려줬다 — 빌드본은 1 을 돌려주는데 (백엔드가 어긋나 있었다).
exitpass=0
exitfail=0
check_exit() {  # 설명, 기대 코드, 명령…
    local what="$1" want="$2"; shift 2
    "$@" > "$TMP/exit.txt" 2>&1 < /dev/null
    local got=$?
    if [ "$got" = "$want" ]; then
        echo "PASS  종료코드/$what  ($got)"; exitpass=$((exitpass+1))
    else
        echo "FAIL  종료코드/$what  (기대 $want, 받음 $got)"
        cat "$TMP/exit.txt"; exitfail=$((exitfail+1)); dfail=$((dfail+1))
    fi
}
printf 'print "안녕"\n'                > "$TMP/ec_ok.my"
printf 'let xs = [1]\nprint xs[9]\n'   > "$TMP/ec_err.my"
printf 'let x = input "? "\nprint x\n' > "$TMP/ec_eof.my"
printf 'let xs = [1]\nprint xs[0]\n'   > "$TMP/ec_nopy.my"
check_exit "정상 실행"     0 "$VENOS" "$TMP/ec_ok.my"
check_exit "잡히지 않은 에러" 1 "$VENOS" "$TMP/ec_err.my"
check_exit "입력이 끊김"   1 "$VENOS" "$TMP/ec_eof.my"
check_exit "topython 정상" 0 "$VENOS" topython "$TMP/ec_ok.my"
check_exit "topython 거절" 1 "$VENOS" topython "$TMP/ec_nopy.my"
check_exit "모르는 인자"   1 "$VENOS" "$TMP/ec_ok.my" --뭐지
check_exit "없는 파일"     1 "$VENOS" "$TMP/없는파일.my"
# 명령어만 치고 파일 이름을 빼먹은 경우 — 예전엔 "파일 없음: build.my" 라는,
# 학생이 만든 적도 없는 이름이 나왔다
check_exit "build 파일 없음"   1 "$VENOS" build
check_exit "topython 파일 없음" 1 "$VENOS" topython
check_exit "run 파일 없음"     1 "$VENOS" run

# ---- 내장 함수·키워드가 모든 곳에 있는가 (tools/check-builtins.js) ----
# 내장 함수 하나를 인터프리터에만 더하고 마는 실수는 조용하다 —
# topython 이 거절하면서 다리가 끊긴다. 소스와 VSCode 문법에서 목록을 뽑아 대조한다.
builtins="건너뜀 (node 없음)"
if command -v node >/dev/null 2>&1; then
    if node tools/check-builtins.js > "$TMP/builtins.txt" 2>&1; then
        builtins=$(cat "$TMP/builtins.txt")
    else
        builtins="실패"
        cat "$TMP/builtins.txt"
        dfail=$((dfail+1))
    fi
fi

# ---- 레슨 트랙 (tools/check-lessons.js) ----
# docs/lessons.js 가 플레이그라운드 레슨과 TUTORIAL 양쪽의 원본이라 여기가 깨지면 둘 다 깨진다.
lessons="건너뜀 (node 없음)"
if command -v node >/dev/null 2>&1; then
    if node tools/check-lessons.js > "$TMP/lessons.txt" 2>&1; then
        lessons="통과"
    else
        lessons="실패"
        cat "$TMP/lessons.txt"
        dfail=$((dfail+1))
    fi
fi

echo
echo "결과: 통과 $pass / 실패 $fail   (파이썬 변환까지 검증 $pytested, 건너뜀 $pyskipped)"
[ -z "$failed_names" ] || echo "실패한 케이스:$failed_names"
echo "셸 편집 모드: $shell_ok"
echo "topython 거절: 통과 $npass"
echo "종료 코드: 통과 $exitpass / 실패 $exitfail"
echo "REPL: $repl_ok"
echo "이름 대조: $builtins"
echo "레슨 트랙: $lessons"
echo "에러 메시지: 통과 $dpass / 실패 $dfail"
[ "$fail" -eq 0 ] && [ "$dfail" -eq 0 ]
