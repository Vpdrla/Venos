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
- **단일 파일 `venos.cpp` (~4,400줄)** 안에 전부 들어 있음: 렉서 → 재귀 하강 파서 → AST → ①트리워킹 인터프리터 ②C++ 트랜스파일러(`build` 명령, g++ 호출) ③파이썬 생성기(`topython`, `PyGen`) ④CLI 셸 ⑤REPL ⑥WASM 진입점.
- 언어 스펙: `VENOS_SPEC.md`(한국어) / `VENOS_SPEC.en.md`(영어) — **기능 추가 시 두 문서 모두 갱신**.
- 검증 프로젝트: `examples/rpg.my` (222줄 텍스트 RPG).
- 웹 플레이그라운드: `docs/` (index.html + venos.js + venos.wasm) → GitHub Pages. 공유 링크(`#code=`), 자동 저장, 레슨 트랙(`#lesson=`) 포함.
- 튜토리얼: **`docs/lessons.js` 가 단일 진실 공급원**. 레슨을 고쳤으면 `node tools/gen-tutorial.js` 로 `TUTORIAL.md`/`TUTORIAL.ko.md` 를 다시 생성할 것 (직접 편집 금지).
- VSCode 확장: `vscode-venos/` — 새 키워드/내장함수 추가 시 tmLanguage도 갱신.

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

**자동화됨**: `tests/run_tests.sh` 가 `tests/cases/*.my` 전체를 **세 방식**(인터프리터 / C++ 빌드본 / topython → python3)으로 실행해 비교하고, CI(`.github/workflows/ci.yml`)가 푸시마다 돌린다. 허용 차이는 러너가 정규화로 흡수: 인터프리터 전용 `=== ===` 배너, catch 메시지의 `[줄 N]` 접두사, 소수 표기(양쪽을 `%g` 로 통일 — 파이썬은 `91.66666666666667`, Venos 는 `91.6667`).
- 파이썬 비교를 건너뛰는 케이스는 러너의 `PY_SKIP` 에 이유와 함께 적혀 있다 (Venos 고유 에러 문구에 기대는 케이스들: errors/bugfixes/fileio/listops).
```bash
tests/run_tests.sh   # 전체 스위트 (C++17 빌드 → 케이스별 인터프리터 vs 빌드본 diff)
```

## 릴리스 내는 법
버전을 올렸으면 태그만 밀면 된다. `.github/workflows/release.yml` 이 세 플랫폼 바이너리를 만들어 GitHub Releases 에 올린다.
```bash
git tag v0.6.0 && git push origin v0.6.0
```
- Linux/Windows 는 ubuntu 러너 한 곳에서 (Windows 는 MinGW 크로스 컴파일), macOS 는 전용 러너에서 유니버설(arm64+x86_64)로 빌드.
- 전부 정적 링크 → 받는 사람은 설치할 게 없음. 릴리스로 나가는 바로 그 바이너리로 differential 스위트를 돌려 검증한다 (Linux·macOS 는 스위트까지, Windows exe 는 아직 빌드만).
- **태그를 못 밀 때는 Actions → Release → Run workflow 에서 `tag` 칸에 `v0.6.0` 을 넣으면** 그 이름으로 태그를 만들고 릴리스까지 낸다. `tag` 를 비우면 빌드·테스트만 하고 릴리스는 안 만든다 (시험 실행).
- `release` 잡은 게시 직전에 **업로드되는 Linux 바이너리를 실제로 한 번 실행**해 본다 (빌드 잡의 스위트는 스테이징 전에 돌기 때문에 아티팩트 왕복 이후는 여기서만 검증된다).
- MinGW 는 메타패키지(`g++-mingw-w64-x86-64`) 말고 **`g++-mingw-w64-x86-64-posix` 하나만** 설치한다 — 메타패키지가 posix/win32 스레딩 변종을 둘 다 끌어와 144MB 를 받기 때문. venos.cpp 는 `_beginthreadex`(Win32 API)만 쓰고 `std::thread` 는 안 써서 변종은 무관하다. 빌드도 `x86_64-w64-mingw32-g++-posix` 로 이름을 명시해 부른다.
- **버전 문자열은 여러 곳에 하드코딩돼 있다** — 태그 전에 같이 고칠 것: `venos.cpp` 헤더 주석과 셸 배너, `VENOS_SPEC.md`/`.en.md` 제목, `vscode-venos/package.json`, README 2종.
- **기능 추가 시 테스트 케이스도 추가할 것.** 에러 케이스는 try/catch 로 잡아 출력으로 만들어 비교 (에러 문구도 양쪽 동일해야 함 — v1.5에서 산술 연산 문구 통일함).
- 케이스에 random()/time() 사용 금지 (비결정적이라 diff 불가).

## 아키텍처 요점
- `Value`: NUM/STR/LIST/MAP/OBJ. 리스트/딕셔너리/객체는 shared_ptr 참조 방식, `copy()`가 깊은 복사(순환 감지). 문자열 불변, UTF-8 글자 단위 인덱싱. **리스트 인덱스는 1부터.**
- 제어 흐름 = `Stmt::exec()` 의 반환값 `Flow{NORMAL,BREAK,CONTINUE,RETURN}` (반환값은 전역 `g_retVal`). **새 Stmt 를 만들면 반드시 Flow 를 올바로 전파할 것** — 블록/조건/try 는 자식 것을 그대로 올리고, 반복문만 BREAK/CONTINUE 를 소비한다. try/catch(LangError)를 **통과**하는 성질은 그대로 (애초에 예외가 아니므로 저절로). 예전엔 예외였는데 `throw` 한 번에 마이크로초가 들어 재귀가 CPython 의 50배 느렸다 (fib(27) 2.0초 → 0.24초로 개선). `exit()` 만 예외(`ExitSignal`) — 프로그램 전체를 끊는 거라 그게 맞다.
- 잡히지 않은 에러는 **호출 경로**("부른 순서: 바깥 (줄 10에서) → ...")까지 보여 준다 — `LangError` 가 생성 시점에 `g_frames` 를 찍어 두고(`callPath()`), `printError(const LangError&)` 가 출력. `DepthGuard` 가 프레임을 쌓으므로 새 호출 경로를 만들면 거기도 `DepthGuard(line, name)` 를 쓸 것. 인터프리터 전용이라(빌드본은 줄번호 자체가 없다) 테스트 러너가 `부른 순서:` 줄을 정규화로 걷어낸다.
- 에러 문구의 한국어 조사는 `josa(단어, "과", "와")` 로 고른다 (본체와 RUNTIME 양쪽에 같은 함수가 있다). 직접 "와(과)" 를 쓰지 말 것.
- 에러 메시지는 `lineTag(line)` 사용 (직접 "[줄 N]" 문자열 만들지 말 것) — import 병합 시 원본 파일 좌표(`[utils.my 줄 3]`)로 자동 변환됨 (`g_lineMap`). 에러 밑에 해당 코드 줄 표시는 `printError()` + `g_srcLines`.
- 실행은 `runOnBigStack`(128MB 전용 스택 스레드) 경유 — 재귀 한도(2000) 전에 세그폴트 방지. WASM에선 스레드 없이 직접 실행(링크 시 TOTAL_STACK 32MB).
- 트랜스파일러: 메서드는 클래스별 정적 함수 `m_클래스_메서드` + (이름,인자수)별 디스패처(수제 vtable). 식별자 맹글링 u_/f_ + non-ASCII hex. 대입 좌변은 접근자 체인(idx_mid/idx_put/fld_mid/fld_put).
- `import`는 파싱 전 텍스트 병합 (`expandImports`, 중복 자동 스킵).

## 지뢰밭 (이미 밟고 고친 것들 — 재발 금지)
- windows.h가 `IN`/`OUT`을 빈 매크로로 정의 → enum은 `Tok::INKW`, include 뒤 `#undef IN/OUT` + `#ifndef NOMINMAX` 가드 유지 (본체와 RUNTIME 문자열 양쪽).
- Windows 콘솔 한글: 셸은 ReadConsoleW, **생성 exe의 RUNTIME에도 동일 로직(rt_readline) 이식돼 있음** — input 관련 수정 시 양쪽 유지.
- Emscripten은 기본으로 C++ 예외 catch 비활성 → WASM 빌드에 `-fexceptions -s DISABLE_EXCEPTION_CATCHING=0` 필수 (없으면 return/break가 전부 죽음).
- **EM_JS 안에서 힙 문자열을 읽을 때 `UTF8ToString(p)` 을 그냥 쓰지 말 것.** emscripten 은 16바이트가 넘는 문자열만 `TextDecoder.decode(HEAPU8.subarray(...))` 로 푸는데, 최신 Chrome 이 성장 가능한 wasm 힙을 **resizable ArrayBuffer** 로 주면 TextDecoder 가 거부한다 (`must not be resizable`). 긴 `input` 프롬프트가 전부 이걸로 죽었다 — 짧은 건 수동 루프로 가서 멀쩡해 더 헷갈린다. **`HEAPU8.slice(p, end)` 사본을 디코드할 것** (`js_prompt_raw` 참고). `-sTEXTDECODER=0` 은 이 emscripten 에서 지원 중단(`#error`)이라 빌드 플래그로는 못 피한다.
- input 프롬프트는 개행이 없는 부분 줄이라 emscripten stdout 버퍼에 남는다. 실행이 도중에 죽으면 **다음 실행 첫 줄에 붙어 나온다** → 플레이그라운드가 새 실행 전에 `venos_flush()` 로 비운다.
- **웹에서 `input` 은 반드시 비동기여야 한다.** 동기로 실행하면 `print` 한 글자가 DOM 에만 들어가고 **화면에 칠해지기 전에** 입력창이 떠서, 학생이 무엇을 묻는지 볼 수가 없다 (실제로 제보된 버그). 그래서 `js_prompt_raw` 는 `EM_ASYNC_JS` 이고 빌드에 `-s ASYNCIFY` 가 필요하며, 페이지는 `createVenos({ venosAskInput })` 로 Promise 를 돌려주는 입력 함수를 넘기고 `venos_run` 을 **`ccall(..., { async: true })`** 로 부른다. 셋 중 하나라도 빠지면 조용히 예전 동작으로 돌아간다. (`venosAskInput` 이 없으면 `window.prompt` 로 떨어지는 대비책은 남겨 뒀다.)
- 플레이그라운드/WASM 을 건드렸으면 **`node tools/playground-check.js --future`** 로 확인할 것. `--future` 는 위 resizable 조건을 흉내 내 재현한다 (지금 브라우저로는 그 조건을 만들 수 없다). CI 에는 없다.
- **`TUTORIAL.md`·`TUTORIAL.ko.md` 는 생성 파일이다 — 손으로 고치면 다음 `gen-tutorial.js` 실행 때 조용히 사라진다.** 실제로 한 번 그렇게 영어 번역본을 날렸다. 레슨 코드는 `docs/lessons.js` 의 `code.ko` / `code.en` 에 넣을 것. 레슨 2(variables)만 식별자를 한글로 남긴다 — 그 레슨의 주제가 "이름을 한국어로 지어도 된다"라서다.
- `desc.en` 이 식별자를 이름으로 언급하면(`` `factorial` below ``, `` use `self.name` ``) 영어 코드와 **반드시 같이 고칠 것**. 안 맞으면 설명이 거짓말이 된다.
- 화면 클리어는 `\033[2J\033[3J\033[H` (3J = 스크롤백까지).
- u8string은 C++17/20 타입이 달라서 바이트 복사로 처리 중.

## 현재 상태 & 남은 작업
현재 v0.6.0 (변수/함수/클래스/리스트/딕셔너리/try-catch/import/copy/파일IO/REPL/CLI/에러 줄표시/문자열 보간/리스트 ==·+/`topython` 파이썬 변환). 저장소: github.com/Vpdrla/Venos

**버전 정책 — 임의로 올리지 말 것.** 0.x 는 "아직 안정화 전"이라는 뜻이고, **정식으로 완성됐다고 판단될 때 1.0.0** 을 붙인다 (사용자가 직접 결정). 그 전까지 기능을 추가해도 버전은 그대로 두고, 급한 버그 수정이 필요할 때만 자리수(0.6.1)를 올린다.

- [x] `docs/` 3개 파일 업로드 — **Pages 설정은 사용자가 직접**: Settings→Pages→main `/docs` → https://vpdrla.github.io/Venos/ 확인
- [x] LICENSE 추가 (MIT)
- [x] 테스트 스위트 + CI (tests/run_tests.sh + GitHub Actions)
- [x] VENOS_SPEC.en.md, README.ko.md, examples/rpg.my, vscode-venos/ 추가 (README 깨진 링크 해소)
- [x] README 데모 (GIF — RPG 플레이 → build 26초, docs/demo.gif, 한글 2칸 폭 렌더러로 제작)
- [x] 교육용 1라운드: 플레이그라운드 공유 링크·자동 저장·WASM 로드 실패 처리 + 12단계 레슨 트랙 + TUTORIAL 자동 생성
- [ ] 개발기 블로그 초안 (소재: IN 매크로 사건, 세그폴트→128MB 스택, diff 테스팅, WASM -fexceptions)
- [ ] 커뮤니티 공유: r/ProgrammingLanguages → Show HN → 국내 (플레이그라운드 완성 후)
- [x] 릴리스 자동화 (`.github/workflows/release.yml`) — Linux/Windows/macOS 정적 바이너리 → GitHub Releases. **첫 릴리스 v0.6.0 게시됨** (https://github.com/Vpdrla/Venos/releases/tag/v0.6.0, 태그는 `1ca77d3`, 자산 4개, 전체 런 69초)
- [x] `input` 의 `window.prompt()` 모달 제거 — **Asyncify** 로 해결. 출력창 아래 입력줄이 뜨고, 기다리는 동안 화면이 정상적으로 칠해진다. wasm 471KB → 839KB(1.78배), 브라우저 fib(24) 0.33초(네이티브 0.51초)라 속도는 문제 없음. (Worker+SharedArrayBuffer 는 GitHub Pages 가 COOP/COEP 헤더를 못 줘서 불가)
- [x] 에러 메시지에 오타 제안 (글자 단위 편집 거리 — 변수/대입/함수/내장함수/필드/메서드, 인터프리터와 트랜스파일러 양쪽). 닫히지 않은 `{` 는 파일 끝이 아니라 여는 줄을 가리키고, `if x = 5` 는 `==` 를 안내한다. 회귀 테스트는 `tests/diag/*.my` + `.expected` (러너 2단계)
- [x] `docs/venos.js`·`venos.wasm` 드리프트 방지 — 빌드는 `tools/build-wasm.sh` 로 통일했고, 스크립트가 `docs/venos.wasm.source-sha256` 에 소스 해시를 남긴다. CI 의 `playground-wasm` 잡이 `venos.cpp` 해시와 대조해 **소스만 고치고 WASM 을 안 올린 상태를 실패로 잡는다** + emsdk 로 빌드 자체도 확인. (마지막 수동 빌드: emsdk 6.0.9)
- [ ] Windows 네이티브 CI 잡 — macOS 는 release.yml 에서 유니버설 빌드 + 스위트까지 돌지만, Windows exe 는 크로스 컴파일로 **빌드만** 되고 한 번도 실행되지 않는다 (ReadConsoleW·`IN`/`OUT` 매크로 회피·`_beginthreadex` 가 런타임 미검증). `windows-latest` 에서 스위트를 돌리려면 Git Bash·CRLF·콘솔 한글 인코딩부터 확인해야 함
- [x] 문자열 보간 `"이름: {x}"`, 리스트 `==`(깊은 비교)/`+`(연결) — v0.6.0
- [x] **포지셔닝 확정 + `topython`** — 조사(Portugol/HAGGIS/Pascal/2022 개정 교육과정) → `STRATEGY.md`, `PyGen`, 플레이그라운드 🐍 Python 버튼, 3중 differential
- [x] 교과서 알고리즘 예제집 (`examples/algorithms/`) — 15개, 교과서 의사코드를 주석에 넣고 1:1. **테스트 스위트에 편입**되어 세 방식으로 돌려 비교한다 (러너가 `tests/cases` 와 `examples/algorithms` 를 함께 순회)
- [ ] ~~다음 언어 기능 후보: 일급 함수, 상속~~ — **포지셔닝상 거부**. 음수 인덱스/슬라이스만 재검토 여지 있음
- [x] 리네임: MyLang → **Venos** (문서/배너/바이너리/확장 일괄 치환 완료. 저장소 rename(Settings→Rename→Venos)은 사용자가 직접 — 하기 전까지 README 링크·Pages URL은 새 주소 기준이라 404)
