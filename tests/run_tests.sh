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
# 위에 더해 **에러 밑의 소스 줄 표시**까지 걷어낸다 (`    줄 5 | print p.나의`).
# 그건 인터프리터만 낼 수 있는 것이다 — 빌드본에는 소스가 없다. 진단 케이스를 빌드본과
# 나란히 놓고 **문구까지** 비교할 때 쓴다.
normalize_err() {
    normalize "$1" | grep -v '^    줄 [0-9]\{1,\} | '
}

pass=0; fail=0; pytested=0; pyskipped=0; traced=0
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

    # ---- ①-b 추적 모드 ----
    # venos trace 는 같은 프로그램을 같은 답으로 돌려야 한다. 추적 줄은 stderr 로 가므로
    # stdout 만 받으면 run 과 글자까지 같아야 한다 — 값이 바뀌는 자리마다 손을 댄 기능이라
    # (대입·경로 대입·복합 대입·두 반복문·호출/반환) 조용히 동작을 바꾸기 쉽다.
    rm -f tests/.tmp_*
    "$VENOS" trace "$case_file" < "$input" > "$TMP/trace.txt" 2>"$TMP/tracelines.txt"
    if ! diff <(normalize "$TMP/interp.txt") <(normalize "$TMP/trace.txt") > "$TMP/diff.txt" 2>&1; then
        echo "FAIL  $label  (추적 모드가 출력을 바꿈)"
        cat "$TMP/diff.txt"
        note_fail "$label"; continue
    fi
    [ -s "$TMP/tracelines.txt" ] && traced=$((traced+1))

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
        # 에러가 **났다**는 것만 보면 모자라다 — 같은 실수에 두 백엔드가 다른 말을 해도
        # 조용했다. 실제로 하나 있었다: 필드 오타 제안("혹시 '나이'?")이 인터프리터에만
        # 있었다 (필드 이름은 실행할 때에야 알 수 있어서 CodeGen 의 정적 검사로는 못 잡는데,
        # RUNTIME 에는 제안 기계가 아예 없었다). 지금은 문구까지 나란히 본다.
        if ! diff <(normalize_err "$TMP/diag.txt") <(normalize_err "$TMP/dbrun.txt") > "$TMP/diff.txt" 2>&1; then
            echo "FAIL  진단/$name  (빌드본의 문구가 인터프리터와 다릅니다)"
            head -8 "$TMP/diff.txt"
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

# create 가 **만들어지지도 않은 파일을 "생성됨" 이라고** 말하면 안 된다.
# (없는 폴더 안에 만들려 하면 조용히 실패했고, 학생은 파일이 있다고 믿고 코드를 쳤다)
(
    cd "$TMP/shell" 2>/dev/null || exit 0
    printf 'create 없는폴더/x.my\nexit\n' | "$VENOS_ABS" > create.txt 2>&1
) || true
if [ -f "$TMP/shell/없는폴더/x.my" ] || ! grep -q '만들지 못했습니다' "$TMP/shell/create.txt" 2>/dev/null; then
    shell_ok="실패 (create 가 못 만든 파일을 만들었다고 합니다)"
    tail -3 "$TMP/shell/create.txt" 2>/dev/null
    dfail=$((dfail+1))
fi

# ---- 에디터의 스크롤 뷰어 (:v) ----
# `scrollViewer` 는 **어떤 검사도 한 번도 들어간 적이 없는 코드**다. `readKey()` 에
# 파이프 대비책(u/d/U/D, EOF 는 나가기)이 이미 있는데도 그 길로 들어가 본 적이 없었다 —
# 화면을 그리는 자리라 "돌려 볼 생각"이 안 드는 쪽이다. 여기서 무한 루프가 되면
# 학생의 셸이 멈추고, 스크롤 산술이 틀리면 파일의 일부를 영영 못 본다.
# 이 검사가 잡아야 할 **첫 번째 것이 "안 끝남"** 이라, 있으면 timeout 으로 감싼다
# (macOS 에는 기본으로 없다 — 없으면 그냥 돌리고 CI 의 잡 타임아웃에 맡긴다).
TMO=""
command -v timeout >/dev/null 2>&1 && TMO="timeout 20"
viewer_ok="통과"
(
    mkdir -p "$TMP/viewer" && cd "$TMP/viewer" || exit 0
    awk 'BEGIN { for (i = 1; i <= 40; i++) print "print " i }' > 긴파일.my
    # d d D 로 내려갔다가 q 로 나오고, 그래도 :q 가 정상 저장하는지
    printf 'choose 긴파일.my\ncode\n:v\nd\nd\nD\nq\n:q\nexit\n' \
        | $TMO "$VENOS_ABS" > view.txt 2>&1
    # 빈 파일에서도 죽지 않아야 한다 (maxOff 가 0 이 되는 갈래)
    printf 'create 빈것.my\n:q\ncode\n:v\nd\nD\nq\n:q\nexit\n' \
        | $TMO "$VENOS_ABS" > view_empty.txt 2>&1
) || true
# 스크롤이 실제로 내려갔는가 — 첫 화면(1~18줄)에 없던 줄이 보여야 한다
grep -q 'print 40' "$TMP/viewer/view.txt" 2>/dev/null \
    || viewer_ok="실패 (스크롤해도 뒷부분이 안 보입니다)"
grep -q '빈 파일' "$TMP/viewer/view_empty.txt" 2>/dev/null \
    || viewer_ok="실패 (빈 파일에서 뷰어가 이상합니다)"
if [ "$viewer_ok" = "통과" ]; then
    echo "PASS  셸/스크롤 뷰어 (:v)"
else
    echo "FAIL  셸/스크롤 뷰어  $viewer_ok"
    tail -5 "$TMP/viewer/view.txt" 2>/dev/null
    dfail=$((dfail+1))
    shell_ok="$shell_ok · 뷰어 실패"
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

# **열린 블록에서 빠져나갈 수 있는가.** `{` 를 하나 잘못 열면 연속 입력이 quit 까지
# 삼켜서, 진짜 터미널에서는 Ctrl+C 말고 나갈 길이 없었다 (파이프는 EOF 로 끝나 버려
# 이 검사가 없던 동안 아무도 못 봤다 — 위의 검사도 전부 EOF 로 끝난다).
# 둘을 본다: 빈 줄 두 번으로 **취소하고 이어서 쓸 수 있는가**, quit 으로 **나갈 수 있는가**.
printf 'repl\nlet 합 = 0\nfor i = 1 to 9 {\n\n\n합 = 12345\n합\n:q\nexit\n' \
    | "$VENOS" > "$TMP/repl_esc.txt" 2>&1
grep -q '12345' "$TMP/repl_esc.txt" \
    || { repl_ok="실패 (빈 줄 두 번으로 열린 블록을 취소하지 못합니다)"; dfail=$((dfail+1)); }
# quit 은 연속 입력 중에도 나가야 한다. 안 나가면 뒤의 print 가 REPL 안에서 돌아
# "안나감" 이 찍힌다 — 셸로 떨어졌다면 셸이 모르는 명령이라 그 글자가 안 나온다.
printf 'repl\nfor i = 1 to 9 {\nquit\nprint "안나감"\nexit\n' \
    | "$VENOS" > "$TMP/repl_quit.txt" 2>&1
grep -q '안나감' "$TMP/repl_quit.txt" \
    && { repl_ok="실패 (연속 입력 중 quit 이 먹히지 않습니다)"; dfail=$((dfail+1)); }
[ "$repl_ok" = "통과" ] || { cat "$TMP/repl.txt"; cat "$TMP/repl_esc.txt"; cat "$TMP/repl_quit.txt"; }

# ---- 추적표가 무엇을 찍는가 (tests/trace) ----
# ①-b 는 "추적이 프로그램의 답을 바꾸지 않는가" 만 본다. **추적 줄 자체는 여태 어디와도
# 비교된 적이 없었다** — traceValue 가 망가지거나 줄 번호가 어긋나도 조용했다는 뜻이다.
# stderr 만 비교한다: stdout 과 섞으면 버퍼링 때문에 순서가 플랫폼마다 달라진다.
trpass=0
for case_file in tests/trace/*.my; do
    [ -e "$case_file" ] || break
    name=$(basename "$case_file" .my)
    want="tests/trace/$name.expected"
    if [ ! -f "$want" ]; then
        echo "FAIL  추적/$name  (.expected 가 없습니다)"; dfail=$((dfail+1)); continue
    fi
    "$VENOS" trace "$case_file" 2> "$TMP/trace_out.txt" > /dev/null
    if diff <(tr -d '\r' < "$want") <(tr -d '\r' < "$TMP/trace_out.txt") > "$TMP/diff.txt" 2>&1; then
        echo "PASS  추적/$name"; trpass=$((trpass+1))
    else
        echo "FAIL  추적/$name  (추적 줄이 바뀌었습니다)"
        head -12 "$TMP/diff.txt"; dfail=$((dfail+1))
    fi
done

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

# 파일을 못 열었을 때 나오는 안내. 종료 코드만 보면 문구가 "파일 없음: examples/.my"
# 로 되돌아가도 조용하다 — 학생이 만든 적 없는 이름을 대는 게 원래 문제였다.
check_says() {  # 설명, 출력에 있어야 할 문구, 명령…
    local what="$1" want="$2"; shift 2
    "$@" > "$TMP/says.txt" 2>&1 < /dev/null
    if grep -qF -- "$want" "$TMP/says.txt"; then
        echo "PASS  안내/$what"; exitpass=$((exitpass+1))
    else
        echo "FAIL  안내/$what  ('$want' 가 없음)"
        cat "$TMP/says.txt"; exitfail=$((exitfail+1)); dfail=$((dfail+1))
    fi
}
mkdir -p "$TMP/폴더"
printf 'print "메모장"\n' > "$TMP/메모장.my.txt"
check_says "폴더를 지정했을 때" "폴더입니다" "$VENOS" "$TMP/폴더"
check_says "메모장이 붙인 .txt" ".txt 는 있습니다" "$VENOS" "$TMP/메모장.my"
check_says "topython 도 같은 안내" ".txt 는 있습니다" "$VENOS" topython "$TMP/메모장.my"
# import 를 쓰면 줄 번호가 병합된 글 기준이라 학생의 파일과 안 맞는다 — 에러처럼
# 추적도 원본 좌표(파일 이름 + 그 파일의 줄)로 말해야 한다
check_says "추적이 원본 파일 좌표로" "lib/도우미.my 줄" "$VENOS" trace tests/cases/imports.my

# ---- 공백이 든 경로 ----
# 스위트의 경로에는 공백이 한 번도 없었다 — 학생의 경로는 "내 문서/수업 자료" 다.
# 셋 다 돌아야 하고, **찍어 주는 명령이 붙여 넣어 쓸 수 있어야** 한다 (실행할 때는
# 이미 따옴표로 감쌌는데 화면에 보여 주는 줄은 그대로였다 — 같은 증상, 다른 경로).
space_ok="통과"
SPACED="$TMP/내 문서/수업 자료"
mkdir -p "$SPACED"
printf 'let xs = [3, 1, 2]\nsort(xs)\nprint "정렬:", xs\n' > "$SPACED/정렬 연습.my"
"$VENOS" "$SPACED/정렬 연습.my" > "$TMP/sp1.txt" 2>&1
grep -q '정렬: \[1, 2, 3\]' "$TMP/sp1.txt" || space_ok="실패 (인터프리터)"
"$VENOS" build "$SPACED/정렬 연습.my" run > "$TMP/sp2.txt" 2>&1
grep -q '정렬: \[1, 2, 3\]' "$TMP/sp2.txt" || space_ok="실패 (build run)"
# 붙여 넣어 쓸 수 있는가 — 공백이 있으면 따옴표가 있어야 한다
grep -q '(run: "' "$TMP/sp2.txt" || space_ok="실패 (보여 주는 명령에 따옴표가 없습니다)"
if [ -n "$PY" ]; then
    "$VENOS" topython "$SPACED/정렬 연습.my" > "$TMP/sp3.txt" 2>&1
    grep -q '(run: python3 "' "$TMP/sp3.txt" || space_ok="실패 (topython 의 명령에 따옴표가 없습니다)"
    [ -f "$SPACED/정렬 연습.py" ] && { "$PY" "$SPACED/정렬 연습.py" > "$TMP/sp4.txt" 2>&1
        grep -q '정렬: \[1, 2, 3\]' "$TMP/sp4.txt" || space_ok="실패 (생성 파이썬)"; }
fi
if [ "$space_ok" = "통과" ]; then
    echo "PASS  안내/공백이 든 경로"; exitpass=$((exitpass+1))
else
    echo "FAIL  안내/공백이 든 경로  $space_ok"; exitfail=$((exitfail+1)); dfail=$((dfail+1))
fi

# 학생의 .my 가 늘 LF 로 오지는 않는다 — **메모장이 저장하면 CRLF** 다. 여태 렉서에
# CRLF 소스를 한 번도 안 넣어 봤다 (스위트의 케이스는 전부 LF 고, 윈도우 CI 도
# core.autocrlf false 로 체크아웃한다 — 그래서 이 입력은 어디에도 없었다).
# 세 방식이 다 같은 답을 내야 하고, 에러 밑의 소스 줄에 ^M 이 남으면 안 된다.
# 파일은 **여기서 만든다** — 저장소에 두면 .gitattributes 가 풀리는 날 조용히 LF 가 된다.
printf 'let x = 1\r\nprint "값: {x}"\r\nfor i = 1 to 2 { print i }\r\nprint 없는이름\r\n' > "$TMP/crlf.my"
crlf_ok="통과"
"$VENOS" "$TMP/crlf.my" > "$TMP/crlf.int" 2>&1
if ! grep -q '값: 1' "$TMP/crlf.int"; then crlf_ok="실패 (인터프리터가 CRLF 를 못 읽음)"; fi
if grep -q $'\r' "$TMP/crlf.int"; then crlf_ok="실패 (에러 밑의 소스 줄에 CR 이 남았다)"; fi
if [ -n "$PY" ]; then
    (cd "$TMP" && "$OLDPWD/$VENOS" topython crlf.my) > /dev/null 2>&1 || true
    if [ -f "$TMP/crlf.py" ]; then
        "$PY" "$TMP/crlf.py" > "$TMP/crlf.py.out" 2>&1 || true
        diff <(normalize_err "$TMP/crlf.int") <(normalize_err "$TMP/crlf.py.out") > /dev/null 2>&1 \
            || crlf_ok="실패 (파이썬 변환본의 답이 다름)"
    fi
fi
if [ "$crlf_ok" = "통과" ]; then
    echo "PASS  안내/CRLF 소스 (메모장 저장본)"; exitpass=$((exitpass+1))
else
    echo "FAIL  안내/CRLF 소스  $crlf_ok"; exitfail=$((exitfail+1)); dfail=$((dfail+1))
fi

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

# ---- WASM 드리프트 ----
# CI 의 playground-wasm 잡이 같은 검사를 하지만, 거기까진 10분이 걸린다.
# venos.cpp 만 고치고 docs/ 를 안 올리는 실수를 **여기서** 알려 준다 (실제로 두 번 했다).
# emsdk 가 없어도 해시 비교는 되므로 누구나 같은 답을 본다.
wasm_ok="통과"
if command -v sha256sum >/dev/null 2>&1; then
    want=$(sha256sum venos.cpp | cut -d' ' -f1)
    have=$(cat docs/venos.wasm.source-sha256 2>/dev/null || echo "(없음)")
    if [ "$want" != "$have" ]; then
        wasm_ok="실패 (venos.cpp 가 바뀌었는데 docs/ 의 WASM 이 낡았습니다"
        wasm_ok="$wasm_ok — emsdk 를 source 하고 tools/build-wasm.sh 를 돌린 뒤 docs/ 를 함께 커밋하세요)"
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
# ${} 로 감쌀 것 — macOS 의 bash 3.2 는 "$traced개" 를 변수 이름 `traced개` 로 읽고
# set -u 아래에서 "unbound variable" 로 죽는다 (리눅스 bash 5 에서는 멀쩡해서 안 보였다)
echo "추적표: ${traced}개 케이스에서 run 과 같은 답 (추적 줄은 stderr) · 추적 줄 자체 ${trpass}개 고정"
echo "종료 코드: 통과 $exitpass / 실패 $exitfail"
echo "REPL: $repl_ok"
echo "이름 대조: $builtins"
echo "레슨 트랙: $lessons"
echo "WASM 드리프트: $wasm_ok"
echo "에러 메시지: 통과 $dpass / 실패 $dfail"
[ "$fail" -eq 0 ] && [ "$dfail" -eq 0 ]
