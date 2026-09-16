# Venos — Claude Code 작업 가이드

자작 프로그래밍 언어 Venos의 저장소. 사용자와 Claude가 claude.ai 대화로 v0.1부터 여기까지 만들었고, 이후 작업은 Claude Code로 진행.

## 대화 규칙
- **한국어로 대화한다.** 간결하고 직설적으로 — 장황한 설명, 과한 칭찬, 불필요한 확인 질문 금지.
- GitHub 공개 문서(README, 프로젝트 설명)는 **영어**로 쓰고, 한국어 번역본(`README.ko.md`)을 별도로 둔다.
- 코드 주석과 에러 메시지는 한국어 (언어 자체가 한국어 사용자 대상).

## 포지셔닝 — 이걸 어기지 말 것
**Venos = 실행되는 의사코드 + 블록(엔트리)에서 파이썬으로 건너가는 다리.** 근거와 조사 내용은 `STRATEGY.md`/`.ko.md` 에 있다.
- **범용 언어로 키우지 말 것.** 일급 함수·상속·모듈 시스템·표준 라이브러리·패키지 매니저는 전부 거부한다 — Pascal 이 교육용으로 성공했다가 "진짜 언어"가 되려다 죽은 경로다. 기능 판단 기준은 "있으면 좋은가"가 아니라 **"이게 없으면 교과서 알고리즘을 못 쓰는가"**.
- **키워드는 영어로 유지.** 식별자만 한글 허용. 키워드를 한국어로 바꾸면 구조가 파이썬으로 안 넘어가서 다리 역할이 깨진다 (약속·와글·새싹이 실패한 이유).
- `topython` 은 기능이자 약속이다 — 나가는 길을 직접 파는 언어는 슬그머니 종착지가 될 수 없다.

## 프로젝트 개요
- **단일 파일 `venos.cpp` (~5,500줄)** 안에 전부 들어 있음: 렉서 → 재귀 하강 파서 → AST → ①트리워킹 인터프리터 ②C++ 트랜스파일러(`build` 명령, g++ 호출) ③파이썬 생성기(`topython`, `PyGen`) ④CLI 셸 ⑤REPL ⑥WASM 진입점.
- 언어 스펙: `VENOS_SPEC.md`(한국어) / `VENOS_SPEC.en.md`(영어) — **기능 추가 시 두 문서 모두 갱신**.
- 검증 프로젝트: `examples/rpg.my` (222줄 텍스트 RPG).
- 웹 플레이그라운드: `docs/` (index.html + venos.js + venos.wasm) → GitHub Pages. 공유 링크(`#code=`), 자동 저장, 레슨 트랙(`#lesson=`) 포함.
- 튜토리얼: **`docs/lessons.js` 가 단일 진실 공급원**. 레슨을 고쳤으면 `node tools/gen-tutorial.js` 로 `TUTORIAL.md`/`TUTORIAL.ko.md` 를 다시 생성할 것 (직접 편집 금지).
- VSCode 확장: `vscode-venos/` — 새 키워드/내장함수 추가 시 tmLanguage도 갱신 (`tools/check-builtins.js` 가 스위트에서 대조하므로 빠뜨리면 CI 가 잡는다).

## 빌드/테스트 명령
```bash
# 네이티브 (필수 통과: C++17과 C++20 둘 다)
g++ -std=c++17 -O2 -Wall -o venos venos.cpp
g++ -std=c++20 -O2 -fsyntax-only venos.cpp

# 실행
./venos 파일.my              # 인터프리터
./venos build 파일.my run    # 트랜스파일 → g++ → 실행
./venos topython 파일.my     # 파이썬으로 변환 (.my → .py)

# WASM (플레이그라운드 갱신 시) — 아래 명령을 그대로 담은 **`tools/build-wasm.sh`** 를 쓰면 된다
# (소스 해시까지 같이 남겨 CI 의 드리프트 검사를 통과시킨다). 직접 부를 때는 emcc 가 아니라 **em++**.
# 요즘 emsdk(6.0.8 확인)는 emcc 로 C++ 를 링크하면 operator delete 미정의로 죽는다.
em++ -O2 -std=c++17 -fexceptions -DVENOS_WASM venos.cpp -o docs/venos.js \
  -s EXPORTED_FUNCTIONS=_venos_run,_venos_topython,_venos_flush,_malloc,_free -s EXPORTED_RUNTIME_METHODS=ccall \
  -s DISABLE_EXCEPTION_CATCHING=0 -s ALLOW_MEMORY_GROWTH=1 \
  -s TOTAL_STACK=33554432 -s INITIAL_MEMORY=67108864 \
  -s MODULARIZE=1 -s EXPORT_NAME=createVenos -s ENVIRONMENT=web \
  -s ASYNCIFY -s ASYNCIFY_STACK_SIZE=1048576
```
`ASYNCIFY` 는 빼면 안 된다 — `input` 이 이것 없이는 페이지를 통째로 얼린다 (아래 지뢰밭).

## 철칙: 백엔드 동시 구현 + diff 검증
언어 기능을 추가/수정하면 **반드시 인터프리터와 트랜스파일러(RUNTIME 문자열 + CodeGen) 양쪽에 구현**하고, 같은 프로그램을 두 방식으로 실행해 출력을 diff로 비교한다 (differential testing — 지금까지 코드젠 버그를 여러 개 잡아준 핵심 검증법). **`PyGen`(topython)도 같이 갱신**한다 — 못 옮기는 문법이면 틀린 파이썬을 내지 말고 줄 번호와 함께 거절할 것.

**자동화됨**: `tests/run_tests.sh` 가 `tests/cases/*.my` 와 `examples/algorithms/*.my` 전체를 **세 방식**(인터프리터 / C++ 빌드본 / topython → python3)으로 실행해 비교하고, CI(`.github/workflows/ci.yml`)가 푸시마다 돌린다. 허용 차이는 러너가 정규화로 흡수: 인터프리터 전용 `=== ===` 배너, catch 메시지의 `[줄 N]` 접두사, 소수 표기(양쪽을 `%g` 로 통일 — 파이썬은 `91.66666666666667`, Venos 는 `91.6667`).
- 파이썬 비교를 건너뛰는 케이스는 러너의 `PY_SKIP` 에 이유와 함께 적혀 있다 (Venos 고유 에러 문구에 기대는 케이스들: errors/bugfixes/fileio/listops_errors). 에러 문구에 기대는 줄만 별도 케이스로 떼어내면 나머지는 파이썬까지 검증할 수 있다 — `listops` 를 그렇게 쪼갰다.
```bash
tests/run_tests.sh   # 전체 스위트: 3중 differential + 에러 메시지 + topython 거절 + 내장함수 대조 + 레슨 트랙
tools/sanitize.sh    # ASan+UBSan+Leak 으로 두 백엔드 훑기 + 퍼징 (약 4분, CI 의 sanitize 잡)
                     # 누수 검출은 켜져 있고, 일부러 순환을 만드는 두 케이스(bugfixes,
                     # listops_errors)만 빼 준다 — 전체를 끄면 진짜 누수도 같이 가려진다
                     # 케이스를 임시 폴더로 복사해 돌린다 — 딸린 파일(tests/cases/lib 등)을
                     # 새로 만들면 여기 복사 목록에도 넣을 것 (안 넣으면 CI 에서만 깨진다)
python3 tools/fuzz.py ./venos --minutes 2      # 망가진 입력으로 크래시 찾기
python3 tools/genfuzz.py --rounds 100         # 올바른 프로그램을 만들어 3중 출력 비교
                                              # (회당 5초쯤 — g++ 가 대부분. CI 는 30회를
                                              #  매번 다른 씨앗으로 돌린다. --keep 으로
                                              #  어긋난 입력을 genfuzz-diffs/ 에 남긴다)
```
러너는 여섯 단계다:
1. **3중 differential** — `tests/cases/*.my` 와 `examples/algorithms/*.my` 를 인터프리터 / C++ 빌드본 / topython 파이썬으로 돌려 비교
2. **에러 메시지 회귀** — `tests/diag/*.my` 는 일부러 틀린 프로그램이고 출력이 `.expected` 와 글자까지 같아야 한다. 문구를 고쳤으면 `./venos tests/diag/X.my > tests/diag/X.expected 2>&1` 로 다시 만들 것
3. **셸 편집 모드** — `create`/`code`/`:d`/`:q` 로 파일이 제대로 저장되고 셸의 `run`·`topython` 이 도는지. 화면 문구는 보지 않는다 (사소한 변경에 깨지므로). **`:run` 뒤에는 "엔터를 누르면" 프롬프트가 한 줄을 더 먹는다** — 스크립트로 몰 때 여기 걸린다
4. **topython 거절** — `tests/nopython/*.my` 는 **파이썬이 Venos 와 다르게 답하는** 프로그램이다. `venos topython` 이 줄 번호를 대고 거절해야 하고 출력이 `.expected` 와 같아야 한다. 틀린 파이썬을 내는 건 거절보다 나쁘다 — 학생은 틀린 줄 알 길이 없다
5. **이름 대조** — `node tools/check-builtins.js` (내장 함수는 BUILTIN_NAMES·인터프리터·CodeGen·PyGen·`vscode-venos` 다섯 곳, 키워드는 `KW_*` 상수와 `vscode-venos` 양쪽에서 뽑아 비교. 한 곳만 빠뜨리는 실수가 전부 조용해서 정적 대조로 잡는다)
6. **레슨 트랙** — `node tools/check-lessons.js` (레슨 코드가 ko·en 둘 다 에러 없이 돌고, 설명이 백틱으로 가리키는 이름이 코드에 있고, TUTORIAL 2종이 최신인지)

## 릴리스 내는 법
버전을 올렸으면 태그만 밀면 된다. `.github/workflows/release.yml` 이 세 플랫폼 바이너리를 만들어 GitHub Releases 에 올린다.
```bash
git tag v0.6.0 && git push origin v0.6.0
```
- Linux/Windows 는 ubuntu 러너 한 곳에서 (Windows 는 MinGW 크로스 컴파일), macOS 는 전용 러너에서 유니버설(arm64+x86_64)로 빌드.
- 전부 정적 링크 → 받는 사람은 설치할 게 없음. 릴리스로 나가는 바로 그 바이너리로 differential 스위트를 돌려 검증한다 (Linux·macOS 는 스위트까지, Windows exe 는 여기서 크로스 컴파일이라 빌드만 — 대신 `ci.yml` 의 `windows` 잡이 `windows-latest` 에서 같은 소스를 MinGW 로 빌드해 스위트를 돌린다).
- **태그 없이 시험 실행**: Actions → Release → Run workflow 에서 `tag` 를 비우면 세 플랫폼 빌드 + macOS 스위트까지만 돌고 릴리스는 안 만든다 (`release` 잡이 `if: startsWith(github.ref, 'refs/tags/') || inputs.tag != ''` 로 막혀 있다). macOS(Apple clang, 유니버설)는 평소 CI 에 없으므로, venos.cpp 를 크게 건드렸으면 이걸 한 번 돌려 보는 게 싸다. **브랜치에서도 돌릴 수 있다** — `ref` 를 그 브랜치로 주면 된다.
- **태그를 못 밀 때는 Actions → Release → Run workflow 에서 `tag` 칸에 `v0.6.0` 을 넣으면** 그 이름으로 태그를 만들고 릴리스까지 낸다. `tag` 를 비우면 빌드·테스트만 하고 릴리스는 안 만든다 (시험 실행).
- `release` 잡은 게시 직전에 **업로드되는 Linux 바이너리를 실제로 한 번 실행**해 본다 (빌드 잡의 스위트는 스테이징 전에 돌기 때문에 아티팩트 왕복 이후는 여기서만 검증된다).
- MinGW 는 메타패키지(`g++-mingw-w64-x86-64`) 말고 **`g++-mingw-w64-x86-64-posix` 하나만** 설치한다 — 메타패키지가 posix/win32 스레딩 변종을 둘 다 끌어와 144MB 를 받기 때문. venos.cpp 는 `_beginthreadex`(Win32 API)만 쓰고 `std::thread` 는 안 써서 변종은 무관하다. 빌드도 `x86_64-w64-mingw32-g++-posix` 로 이름을 명시해 부른다.
- **버전 문자열은 여러 곳에 하드코딩돼 있다** — 태그 전에 같이 고칠 것: `venos.cpp` 의 `VENOS_VERSION` 상수(셸 배너·`--version`·`--help` 가 여기서 읽는다)와 파일 맨 위 헤더 주석, `VENOS_SPEC.md`/`.en.md` 제목, `vscode-venos/package.json`, README 2종.
- **기능 추가 시 테스트 케이스도 추가할 것.** 에러 케이스는 try/catch 로 잡아 출력으로 만들어 비교 (에러 문구도 양쪽 동일해야 함 — v1.5에서 산술 연산 문구 통일함).
- 케이스에 random()/time() 사용 금지 (비결정적이라 diff 불가).

## 아키텍처 요점
- `Value`: NUM/STR/LIST/MAP/OBJ. 리스트/딕셔너리/객체는 shared_ptr 참조 방식, `copy()`가 깊은 복사(순환 감지). 문자열 불변, UTF-8 글자 단위 인덱싱. **리스트 인덱스는 1부터.**
- 제어 흐름 = `Stmt::exec()` 의 반환값 `Flow{NORMAL,BREAK,CONTINUE,RETURN}` (반환값은 전역 `g_retVal`). **새 Stmt 를 만들면 반드시 Flow 를 올바로 전파할 것** — 블록/조건/try 는 자식 것을 그대로 올리고, 반복문만 BREAK/CONTINUE 를 소비한다. try/catch(LangError)를 **통과**하는 성질은 그대로 (애초에 예외가 아니므로 저절로). 예전엔 예외였는데 `throw` 한 번에 마이크로초가 들어 재귀가 CPython 의 ~50배 느렸다. 같은 기계에서 직전 커밋과 비교: **fib(27) 2.73초 → 0.23초, 하노이 20단 10.58초 → 1.93초, 300만 루프는 0.222 → 0.215초(변화 없음 — 대조군)**. `exit()` 만 예외(`ExitSignal`) — 프로그램 전체를 끊는 거라 그게 맞다.
- 잡히지 않은 에러는 **호출 경로**("부른 순서: 바깥 (줄 10에서) → ...")까지 보여 준다 — `LangError` 가 생성 시점에 `g_frames` 를 찍어 두고(`callPath()`), `printError(const LangError&)` 가 출력. `DepthGuard` 가 프레임을 쌓으므로 새 호출 경로를 만들면 거기도 `DepthGuard(line, name)` 를 쓸 것. 인터프리터 전용이라(빌드본은 줄번호 자체가 없다) 테스트 러너가 `부른 순서:` 줄을 정규화로 걷어낸다.
- 에러 문구의 한국어 조사는 `josa(단어, "과", "와")` 로 고른다 (본체와 RUNTIME 양쪽에 같은 함수가 있다). 직접 "와(과)" 를 쓰지 말 것.
- 에러 메시지는 `lineTag(line)` 사용 (직접 "[줄 N]" 문자열 만들지 말 것) — import 병합 시 원본 파일 좌표(`[utils.my 줄 3]`)로 자동 변환됨 (`g_lineMap`). 에러 밑에 해당 코드 줄 표시는 `printError()` + `g_srcLines`.
- 실행은 `runOnBigStack`(128MB 전용 스택 스레드) 경유 — 재귀 한도(2000) 전에 세그폴트 방지. WASM에선 스레드 없이 직접 실행(링크 시 TOTAL_STACK 32MB).
- **웹은 재귀 한도가 200** (`MAX_RECURSION`, `VENOS_WASM` 일 때). `TOTAL_STACK` 은 선형 메모리의 그림자 스택이지 브라우저 호출 스택이 아니라서, 2000 을 그대로 두면 한도에 닿기 전에 V8 이 `RangeError: Maximum call stack size exceeded` 를 던진다 — 학생에게는 알아볼 수 없는 영어 메시지다. 실측: 450 이 두 번 통과했다가 380 이 한 번 실패했다 — 넘어가는 지점이 실행마다 다르고, 한 번 넘치면 그 페이지에서 회복되지 않는다. 그래서 여유를 크게 뒀다. **스택이 터지면 `DepthGuard` 소멸자가 안 돌아 `g_frames` 가 남는다** — `runSource` 가 시작할 때 비운다 (안 비우면 다음 실행의 에러에 앞 실행 함수들이 섞여 나온다). `topython` 이 내는 `setrecursionlimit` 은 `RECURSION_DESKTOP` 기준이라 웹에서 변환해도 같은 파이썬이 나온다. `tools/playground-check.js` 가 한도 안쪽/바깥쪽을 둘 다 확인한다.
- **CPython 대비 실측** (같은 프로그램을 `topython` 으로 옮겨 비교, 같은 기계): 선택정렬 1500개 3.0배 · 에라토스테네스 20만 2.8배 · 재귀 fib(27) 6.5배 느리고, 딕셔너리 카운팅 6만은 0.8배(더 빠름). 예외 하나 — 반복문 안의 `s = s + 글자` 는 **길이의 제곱**이다 (2만 자 0.07초, 16만 자 4.9초). 파이썬은 참조가 하나일 때 제자리에서 늘려 선형이라 여기만 점근이 다르다. **고치지 않기로 한 결정**이고 근거는 `STRATEGY.md` §6 에 있다 — 다시 꺼내기 전에 그걸 읽을 것.
- 트랜스파일러: 메서드는 클래스별 정적 함수 `m_클래스_메서드` + (이름,인자수)별 디스패처(수제 vtable). 식별자 맹글링 u_/f_ + non-ASCII hex. 대입 좌변은 접근자 체인(idx_mid/idx_put/fld_mid/fld_put).
- `import`는 파싱 전 텍스트 병합 (`expandImports`, 중복 자동 스킵). **경로는 그 import 를 쓴 파일 기준**으로 푼다 — cwd 기준이면 `venos 프로젝트/main.my` 를 폴더 밖에서 못 돌린다 (실제 버그였다). 에러에 쓰는 이름(`label`)은 import 에 적힌 그대로 두고, 여는 경로(`path`)만 푼다. `tests/cases/imports.my` + `tests/cases/lib/` 가 3중 differential 로 지킨다.

## 지뢰밭 (이미 밟고 고친 것들 — 재발 금지)
- **윈도우는 `main` 의 argv 를 ANSI 코드페이지로 준다** → 한글 파일 이름이 `?` 로 뭉개져 "파일 없음: ??.my" 가 된다. `useUtf8Argv()` 가 `CommandLineToArgvW` 로 명령줄을 다시 받아 UTF-8 로 바꿔 끼운다. 새로 argv 를 읽는 코드를 넣을 때 이 변환 뒤라는 걸 전제할 것.
- **`venos build` 는 윈도우에서 `-static` 으로 링크한다.** 안 그러면 만들어진 exe 가 `libstdc++-6.dll` 을 PATH 에서 찾아야 하고, 다른 MinGW 의 libstdc++ 이 먼저 걸리면 표준 라이브러리가 조용히 오동작한다. 학생이 친구에게 exe 를 그냥 건넬 수 있는 효과도 있다.
- **언어의 파일 내장함수(`readfile`/`writefile`/`appendfile`/`exists`)는 `openFile()`(본체) / `rt_open()`(RUNTIME) 으로만 열 것.** (셸의 `create`/`show`/`code` 와 import 로더는 `std::ifstream` 을 쓴다 — 윈도우 CI 의 `진단/import_없는파일` 케이스와 "Korean filenames, end to end" 단계가 그 경로를 지키므로 그대로 둔다. 새 파일 접근을 만들 때는 둘 중 어느 쪽인지 먼저 정할 것.) 둘 다 윈도우에서 `_wfopen` + 넓은 경로, 바이너리 모드다. `std::ifstream(문자열)` 은 윈도우에서 한글 경로를 못 열고, **`std::ifstream(std::filesystem::path)` 는 MinGW 빌드에서 없는 파일도 "열렸다"고 답한다** — 트랜스파일 빌드본의 `exists()` 가 전부 참이 되고 `readfile()` 이 에러를 안 냈다 (윈도우 CI 가 잡음). `exists()` 는 양쪽 다 `filesystem::is_regular_file`. 바이너리 모드인 이유는 텍스트 모드의 CRLF 변환이 백엔드마다 다르게 걸리지 않게 하려는 것.
- **생성 파이썬의 출력 인코딩**: 윈도우 파이썬은 stdout 을 로케일 코드페이지로 인코딩해서 한글 print 가 `UnicodeEncodeError` 로 죽는다. 비-ASCII 문자열 리터럴이 있으면 PyGen 이 `sys.stdout.reconfigure(encoding="utf-8")` 를 넣는다 (`sawNonAscii`).
- windows.h가 `IN`/`OUT`을 빈 매크로로 정의 → enum은 `Tok::INKW`, include 뒤 `#undef IN/OUT` + `#ifndef NOMINMAX` 가드 유지 (본체와 RUNTIME 문자열 양쪽).
- Windows 콘솔 한글: 셸은 ReadConsoleW, **생성 exe의 RUNTIME에도 동일 로직(rt_readline) 이식돼 있음** — input 관련 수정 시 양쪽 유지.
- Emscripten은 기본으로 C++ 예외 catch 비활성 → WASM 빌드에 `-fexceptions -s DISABLE_EXCEPTION_CATCHING=0` 필수 (없으면 return/break가 전부 죽음).
- **EM_JS 안에서 힙 문자열을 읽을 때 `UTF8ToString(p)` 을 그냥 쓰지 말 것.** emscripten 은 16바이트가 넘는 문자열만 `TextDecoder.decode(HEAPU8.subarray(...))` 로 푸는데, 최신 Chrome 이 성장 가능한 wasm 힙을 **resizable ArrayBuffer** 로 주면 TextDecoder 가 거부한다 (`must not be resizable`). 긴 `input` 프롬프트가 전부 이걸로 죽었다 — 짧은 건 수동 루프로 가서 멀쩡해 더 헷갈린다. **`HEAPU8.slice(p, end)` 사본을 디코드할 것** (`js_prompt_raw` 참고). `-sTEXTDECODER=0` 은 이 emscripten 에서 지원 중단(`#error`)이라 빌드 플래그로는 못 피한다.
- input 프롬프트는 개행이 없는 부분 줄이라 emscripten stdout 버퍼에 남는다. 실행이 도중에 죽으면 **다음 실행 첫 줄에 붙어 나온다** → 플레이그라운드가 새 실행 전에 `venos_flush()` 로 비운다.
- **실행에서 빠져나갈 길이 있어야 한다 — 입력을 기다릴 때도, 계산만 돌 때도.** 두 갈래다.
  - 입력 대기 중: ⏹ Stop 과 Esc 가 입력 대신 `VENOS_STOP`(`\x01venos-stop`)을 돌려주고, `readLine` 의 WASM 분기가 그걸 보면 `ExitSignal` 을 던진다.
  - 계산 중: `pumpWeb()` 이 루프 세 종류(`While`/`For`/`ForEach`)와 함수 호출(`DepthGuard`)마다 불리면서, **1024번에 한 번 시계를 보고 100ms 마다 한 번 `emscripten_sleep(0)` 로 브라우저에 한 턴을 준다.** 그 턴에 ⏹ 의 클릭이 처리되고(`Module.venosStopRequested`), 돌아온 자리에서 `ExitSignal` 을 던진다. 둘 다 `exit()` 와 같은 길이라 출력 버퍼와 호출 프레임이 정리된다.
  - **양보는 화면도 같이 푼다** — 예전엔 긴 프로그램의 출력이 끝날 때까지 한 줄도 안 보였다. 그래서 `playground-check.js` 의 계산-루프 검사는 "출력이 도중에 보인다"를 기다리는 것으로 양보가 도는지까지 확인한다.
  - **비용 실측**(같은 기계, 크로미움, 양보 없음 대비): 빈 루프 300만 1.05배 · `while` 300만 1.05배 · 선택정렬 800 1.04배 · 재귀 fib(24) 1.01배. wasm 은 987,880 → 989,673 바이트(+0.18%) — Asyncify 가 이미 전 프로그램을 계측하고 있어서 늘어난 건 pump 자신뿐이다. 대부분의 비용은 카운터가 아니라 양보 그 자체(100ms 마다 이벤트 루프 한 바퀴)다.
  - `Module.venosStopRequested` 는 **새 실행 시작 때 페이지가 꺼야 한다** (`run()`). 안 끄면 앞 실행에서 누른 ⏹ 가 다음 실행을 곧바로 죽인다.
- **웹에서 `input` 은 반드시 비동기여야 한다.** 동기로 실행하면 `print` 한 글자가 DOM 에만 들어가고 **화면에 칠해지기 전에** 입력창이 떠서, 학생이 무엇을 묻는지 볼 수가 없다 (실제로 제보된 버그). 그래서 `js_prompt_raw` 는 `EM_ASYNC_JS` 이고 빌드에 `-s ASYNCIFY` 가 필요하며, 페이지는 `createVenos({ venosAskInput })` 로 Promise 를 돌려주는 입력 함수를 넘기고 `venos_run` 을 **`ccall(..., { async: true })`** 로 부른다. 셋 중 하나라도 빠지면 조용히 예전 동작으로 돌아간다. (`venosAskInput` 이 없으면 `window.prompt` 로 떨어지는 대비책은 남겨 뒀다.)
- 플레이그라운드/WASM 을 건드렸으면 **`node tools/playground-check.js --future`** 로 확인할 것. `--future` 는 위 resizable 조건을 흉내 내 재현한다 (지금 브라우저로는 그 조건을 만들 수 없다). **기본 모드는 CI 의 `playground-wasm` 잡이 실제 브라우저로 돌린다** — `--future` 만 손으로 돌리면 된다.
- **`TUTORIAL.md`·`TUTORIAL.ko.md` 는 생성 파일이다 — 손으로 고치면 다음 `gen-tutorial.js` 실행 때 조용히 사라진다.** 실제로 한 번 그렇게 영어 번역본을 날렸다. 레슨 코드는 `docs/lessons.js` 의 `code.ko` / `code.en` 에 넣을 것. 레슨 2(variables)만 식별자를 한글로 남긴다 — 그 레슨의 주제가 "이름을 한국어로 지어도 된다"라서다.
- `desc.en` 이 식별자를 이름으로 언급하면(`` `factorial` below ``, `` use `self.name` ``) 영어 코드와 **반드시 같이 고칠 것**. 안 맞으면 설명이 거짓말이 된다.
- **플레이그라운드는 교실 태블릿에서도 쓸 수 있어야 한다.** `@media (pointer: coarse)` 로 버튼을 44px 이상(손끝 권장 크기), 편집기·입력 칸 글꼴을 16px 이상으로 올린다 — 16px 미만이면 **iOS 사파리가 포커스 때 화면을 확대**해 버린다. `tools/playground-check.js` 가 768×1024 터치 컨텍스트로 확인한다 (가로 스크롤도 같이).
- **`hidden` 속성은 작성자 스타일에 그냥 진다.** `#inputLine { display: flex }` 가 브라우저 기본 스타일의 `[hidden] { display: none }` 을 덮어써서, **빈 입력줄이 늘 떠 있었다** (속성은 `hidden=true` 라 코드만 읽어서는 안 보인다). id 로 `display` 를 주는 요소를 `hidden` 으로 감추려면 `#그것[hidden] { display: none }` 을 같이 쓸 것. `playground-check.js` 는 속성이 아니라 **실제 높이**로 확인한다. ⏹ Stop 은 이 일로 툴바로 옮겼다 — 입력줄 안에 두면 정작 멈추고 싶은 계산 루프에서는 보이지도 않는다.
- **UI 안내문이 기능보다 오래 산다.** Asyncify 로 `input` 모달을 없애고도 툴바 안내와 레슨 3 설명에 "a dialog appears" 가 그대로 남아 있었다. 동작을 바꾸면 `docs/index.html` 의 `.hint`·예제 주석과 `docs/lessons.js` 의 `desc` 를 같이 뒤질 것 (레슨을 고쳤으면 `node tools/gen-tutorial.js`).
- **플레이그라운드의 에러 판정은 `"!! "` (느낌표 둘 + 공백)로 한다.** 공백을 빼면 `print "!!! 고블린이 나타났다!"` 같은 학생 출력이 빨간 에러로 칠해진다 — `examples/rpg.my` 가 실제로 그렇게 찍는다. Venos 가 내는 에러는 전부 `!! 에러: `·`!! codegen error: ` 처럼 공백이 붙는다.
- 화면 클리어는 `\033[2J\033[3J\033[H` (3J = 스크롤백까지).
- u8string은 C++17/20 타입이 달라서 바이트 복사로 처리 중.
- **재귀 하강 파서에는 깊이 제한이 있어야 한다** (`MAX_NEST` 200, `NestGuard`). 없으면 `((((((...` 같은 입력에 그대로 재귀해 세그폴트다. 인터프리터는 128MB 스택 스레드 덕에 버티지만 `topython`·`build` 는 메인 스레드에서 파싱해 5천 단계에 죽었다 — **같은 파일이 실행은 되는데 변환만 죽는** 상태였다. 파서에 새 재귀 지점을 만들면 (`parseUnary`/`parseNot`/`parseBlock` 바깥에) 가드를 같이 넣을 것. 카운터가 전역인 이유는 문자열 보간 `{}` 안이 별도 `Parser` 로 파싱되기 때문.
- 에러 메시지에 사용자 문자열을 끼워 넣을 때는 `ellipsize()` 로 자를 것. 안 그러면 긴 줄 하나가 정작 읽어야 할 설명을 화면 밖으로 밀어낸다 (`print (((...5천개...` 가 197KB 를 쏟았다). **그물은 `LangError` 생성자(300자)와 RUNTIME 의 `RunErr`(`rt_cut`, 같은 300자)에도 쳐 뒀다** — 3천 자짜리 식별자가 9KB 에러 한 줄을 만들던 걸 거기서 막는다. 실제 문구 중 가장 긴 게 100자 남짓이라 여유가 크다.
- 문자열 보간 안의 식은 **하위 `Parser`** 가 파싱한다 — 토큰의 줄 번호를 바깥 줄로 덮어쓰지 않으면 `"{없는변수}"` 의 에러가 늘 "줄 1" 을 가리킨다.
- **다른 언어의 버릇은 이름을 불러 줄 것.** `elif`·`def`·`var`·`True`·`None`·`this`·`length`·`range` 같은 이름은 `foreignNames()` 에, `**`·`^`·`!`·`;`·`//`·`/*`·`<>`·`&&`·`++` 는 렉서에 있다. `suggestName()` 이 먼저 `foreignHint()` 를 보므로 **변수·함수·필드·메서드 자리가 한 번에 좋아진다** — 새 안내를 넣을 때도 거기에 넣을 것. `"abc".upper()` 는 `methodHint()` 가 인터프리터와 생성 코드 양쪽에 같은 문구를 준다.
- **선언 없는 대입은 백엔드마다 시점이 다르다** — 인터프리터는 그 줄이 돌 때 잡을 수 있는 에러, `build` 와 `topython` 은 아예 거절(정적으로 알 수 있으니까). 파이썬은 그냥 새 변수를 만들어 버리므로 `topython` 이 통과시키면 **다른 프로그램**이 된다 (생성 퍼저가 찾았다). 인터프리터도 거절하게 바꾸면 세 곳이 같아지지만 그건 언어 의미를 바꾸는 일이라 사용자 판단으로 남겨 뒀다.
- **`for x in xs` 는 루프에 들어갈 때의 값을 돈다.** 파이썬의 `for` 는 살아 있는 객체를 돌기 때문에 몸통에서 `push` 하면 무한 루프가 된다 — PyGen 이 **몸통이 그 이름을 건드릴 때만** `list(...)` 로 감싼다 (몸통을 먼저 만들어 이름이 나오는지 보는 방식. 예제집 16개에서는 한 줄도 안 감싼다). `_iter` 는 딕셔너리·리스트 모두 사본을 주므로 그쪽은 덧씌우지 않는다.
- **파이썬이 Venos 와 다르게 답하는 자리**를 조용히 넘기지 말 것. 실제로 다섯 군데가 그랬다: `xs[0]`(파이썬은 마지막 원소), `replace(s,"",r)`·`find(s,"")`(글자 사이마다), `min("a","b")`(사전순), `substr(s,0,n)`. **인자가 리터럴이면 변환을 거절**하고(`noEmptyStr`/`numbersOnly`/`indexGet` 의 숫자 검사), 아니면 검사하는 도우미(`_replace`/`_find`/`_substr`/`_idx`)로 보낸다. 변수 인덱스 `A[i-1]` 만은 예외로 그대로 둔다 — 읽히는 파이썬이 이 기능의 존재 이유라서다 (근거는 `STRATEGY.md` §6, 생성 파일 머리말에 한계를 적어 둔다).
- CLI 가 **남는 인자를 조용히 버리지 않게 할 것**. `venos topython 정렬.my -o 결과.py` 가 `-o` 를 무시하고 엉뚱한 곳에 쓰고 있었다 (`extra()` 로 거절한다).
- `venos build` 의 실행 명령에 무조건 `./` 를 붙이지 말 것 — 절대 경로면 `.//home/...` 이 되어 "not found" 다. 경로에 `/` 가 없을 때만 붙이고, 실행할 때는 따옴표로 감싼다(공백 있는 폴더).
- `docs/index.html` 에 NUL 바이트를 넣지 말 것. `pristine` 센티널로 `'\0'` 을 쓰다가 파일이 grep 에 "binary" 로 잡혔다 (지금은 `null`).

## 현재 상태 & 남은 작업
현재 v0.6.0 (변수/함수/클래스/리스트/딕셔너리/try-catch/import/copy/파일IO/REPL/CLI/에러 줄표시/문자열 보간/리스트 ==·+/`topython` 파이썬 변환). 저장소: github.com/Vpdrla/Venos

태그 이후 더해진 것 (다음 릴리스에 들어갈 것들): 문자열 반복 `"*" * n`, 리스트의 `has`/`find`, `reverse`, `round(x, 자릿수)`, 리터럴 끝의 쉼표, `--version`/`--help`, 오타 제안과 호출 경로가 붙은 에러, 나머지 연산의 부호 통일, 재귀 8배 속도 개선, `examples/algorithms/` 16개, 윈도우 버그 4종 수정.

**버전 정책 — 임의로 올리지 말 것.** 0.x 는 "아직 안정화 전"이라는 뜻이고, **정식으로 완성됐다고 판단될 때 1.0.0** 을 붙인다 (사용자가 직접 결정). 그 전까지 기능을 추가해도 버전은 그대로 두고, 급한 버그 수정이 필요할 때만 자리수(0.6.1)를 올린다.

- [x] `docs/` 3개 파일 업로드 — **Pages 설정은 사용자가 직접**: Settings→Pages→main `/docs` → https://vpdrla.github.io/Venos/ 확인
- [x] LICENSE 추가 (MIT)
- [x] 테스트 스위트 + CI (tests/run_tests.sh + GitHub Actions)
- [x] VENOS_SPEC.en.md, README.ko.md, examples/rpg.my, vscode-venos/ 추가 (README 깨진 링크 해소)
- [x] README 데모 (GIF — RPG 플레이 → build 26초, docs/demo.gif, 한글 2칸 폭 렌더러로 제작)
- [x] 교육용 1라운드: 플레이그라운드 공유 링크·자동 저장·WASM 로드 실패 처리 + 13단계 레슨 트랙(마지막은 `topython` 으로 건너가기) + TUTORIAL 자동 생성
- [x] 개발기 블로그 초안 — `DEVLOG.md` / `DEVLOG.ko.md`. 줄기는 "버그는 전부 그걸 볼 수 있는 도구를 만든 날 나왔다" (도구 아홉 개와 각각이 찾은 것). **초안이니 사용자가 다듬어 쓸 것.** 원래 적어 둔 소재 (소재: IN 매크로 사건, 세그폴트→128MB 스택, diff 테스팅, WASM -fexceptions, **return 마다 C++ 예외를 던져 재귀가 CPython 의 50배였던 것**, **윈도우 CI 를 처음 켠 날 잡힌 버그 3개 — argv 가 ANSI 라 한글 파일명을 못 열던 것 / 트랜스파일본이 한글 경로를 못 열던 것 / 생성 파이썬이 한글 출력에 죽던 것**, **f-string 이 파이썬 표기를 새어 나가게 하던 것**)
- [ ] 커뮤니티 공유: r/ProgrammingLanguages → Show HN → 국내 (플레이그라운드 완성 후)
- [x] 릴리스 자동화 (`.github/workflows/release.yml`) — Linux/Windows/macOS 정적 바이너리 → GitHub Releases. **첫 릴리스 v0.6.0 게시됨** (https://github.com/Vpdrla/Venos/releases/tag/v0.6.0, 태그는 `1ca77d3`, 자산 4개, 전체 런 69초)
- [x] `input` 의 `window.prompt()` 모달 제거 — **Asyncify** 로 해결. 출력창 아래 입력줄이 뜨고, 기다리는 동안 화면이 정상적으로 칠해진다. wasm 471KB → 839KB(1.78배), 브라우저 fib(24) 0.33초(네이티브 0.51초)라 속도는 문제 없음. (Worker+SharedArrayBuffer 는 GitHub Pages 가 COOP/COEP 헤더를 못 줘서 불가)
- [x] 에러 메시지에 오타 제안 (글자 단위 편집 거리 — 변수/대입/함수/내장함수/필드/메서드, 인터프리터와 트랜스파일러 양쪽). 닫히지 않은 `{` 는 파일 끝이 아니라 여는 줄을 가리키고, `if x = 5` 는 `==` 를 안내한다. 회귀 테스트는 `tests/diag/*.my` + `.expected` (러너 2단계)
- [x] `docs/venos.js`·`venos.wasm` 드리프트 방지 — 빌드는 `tools/build-wasm.sh` 로 통일했고, 스크립트가 `docs/venos.wasm.source-sha256` 에 소스 해시를 남긴다. CI 의 `playground-wasm` 잡이 `venos.cpp` 해시와 대조해 **소스만 고치고 WASM 을 안 올린 상태를 실패로 잡는다** + emsdk 로 빌드 자체도 확인. (마지막 수동 빌드: emsdk 6.0.9)
- [x] Windows 네이티브 CI 잡 (`ci.yml` 의 `windows`) — MinGW 정적 빌드 + 전체 스위트를 Git Bash 에서. **첫 실행에 실제 버그 3개를 잡았다** (아래 지뢰밭 참고). 체크아웃 전에 `core.autocrlf false`, 러너는 `.exe` 이름과 CRLF 를 흡수한다
- [x] 문자열 보간 `"이름: {x}"`, 리스트 `==`(깊은 비교)/`+`(연결) — v0.6.0
- [x] **포지셔닝 확정 + `topython`** — 조사(Portugol/HAGGIS/Pascal/2022 개정 교육과정) → `STRATEGY.md`, `PyGen`, 플레이그라운드 🐍 Python 버튼, 3중 differential
- [x] 교과서 알고리즘 예제집 (`examples/algorithms/`) — 16개, 교과서 의사코드를 주석에 넣고 1:1. **테스트 스위트에 편입**되어 세 방식으로 돌려 비교한다 (러너가 `tests/cases` 와 `examples/algorithms` 를 함께 순회)
- [ ] ~~다음 언어 기능 후보: 일급 함수, 상속~~ — **포지셔닝상 거부**. 음수 인덱스/슬라이스만 재검토 여지 있음
- [x] 리네임: MyLang → **Venos** (문서/배너/바이너리/확장 일괄 치환 완료. 저장소 rename(Settings→Rename→Venos)은 사용자가 직접 — 하기 전까지 README 링크·Pages URL은 새 주소 기준이라 404)
