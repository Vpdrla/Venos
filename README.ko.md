# Venos

[![CI](https://github.com/Vpdrla/Venos/actions/workflows/ci.yml/badge.svg)](https://github.com/Vpdrla/Venos/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

*[English](README.md) | 한국어*

**▶ [브라우저에서 바로 써보기 — 설치 불필요](https://vpdrla.github.io/Venos/)**

![데모: 예제 RPG 플레이 + 네이티브 빌드](docs/demo.gif)

**실행되는 의사코드.** Venos 는 칠판이나 교과서에 알고리즘을 적는 방식 그대로 씁니다 —
`if x >= 90 then`, `for i = 1 to 10`, `while x > 0 do`. 다만 이건 진짜로 실행됩니다.

그리고 종착지가 아니라 **다리**입니다. 블록 코딩(엔트리·스크래치)에서 텍스트 언어로 넘어가는
길목에서 초보자는 낯선 문법, 영어로만 되는 이름, 읽을 수 없는 에러 메시지에 막힙니다. Venos 는
프로그래밍이 실제로 어떻게 돌아가는지는 숨기지 않으면서 그 세 가지만 걷어내고,
때가 되면 넘겨줍니다 — **`venos topython` 이 Venos 프로그램을 읽을 수 있는 파이썬으로 옮겨 줍니다.**

단일 C++ 파일(~4,400줄)에 백엔드 셋을 구현했습니다: 트리워킹 인터프리터, 독립 실행 파일을 만드는
C++ 트랜스파일러, 그리고 파이썬 생성기.

```
func fib(n) {
    if n <= 2 then { return 1 }
    return fib(n - 1) + fib(n - 2)
}

for i = 1 to 10 {
    print "fib({i}) = {fib(i)}"    # 문자열 보간
}

try {
    let n = num(input "숫자 입력: ")
    print "10 /", n, "=", 10 / n
} catch 오류 {
    print "문제 발생:", 오류
}
```

식별자는 어떤 언어로든 쓸 수 있어서(완전한 UTF-8 지원) 클래스, 함수, 변수를
한국어로 자연스럽게 만들 수 있습니다:

```
class 사람 {
    func init(이름) { self.이름 = 이름 }
    func 인사() { print "안녕, 나는 " + self.이름 }
}
사람("미르").인사()
```

## 특징

- **읽히는 문법** — `if x > 5 then { }`, `for i = 1 to 10`, `while x > 0 do { }`; 생략 가능한 채움 키워드(`then`, `do`) 덕분에 코드가 의사코드처럼 읽힘
- **세 가지 실행 방식** — 즉시 피드백용 인터프리터, 트랜스파일러(`.my` → C++ → g++로 네이티브 실행 파일), 파이썬 생성기(`.my` → `.py`). 셋 다 같은 프로그램에 같은 출력을 내도록 differential testing으로 검증
- **나가는 길** — `venos topython` 이 내 프로그램을 그대로 파이썬으로 옮겨 줍니다. 여기서 배운 게 버려지지 않아요
- **진짜 프로그램을 짤 만큼의 언어** — 함수(재귀, 호이스팅), 클래스(생성자, 메서드, `self`), 리스트/딕셔너리(참조 방식, `==` 깊은 비교, `+` 리스트 연결, `copy()` 깊은 복사), 문자열 보간(`"이름: {x}"`), UTF-8 글자 단위 문자열, `try/catch`, `import`, 파일 입출력, 내장 함수 30개+
- **친절한 에러** — 줄 번호가 붙는 에러 메시지, `import` 사용 시 원본 파일 좌표 표시(`[utils.my 줄 3]`). 미정의 변수·잘못된 인자 개수는 빌드 시점에 잡아줌
- **내장 개발 환경** — 파일 관리, 에디터(방향키 스크롤 뷰어, 붙여넣기 모드), 원커맨드 실행/빌드가 되는 CLI 셸

## Venos 에서 파이썬으로

Venos 는 마지막 언어가 되고 싶어 하지 않습니다. `venos topython` 은 같은 프로그램을 읽을 수 있는
파이썬으로 옮겨 줍니다. 변수·함수 이름도 그대로 살아남습니다 — 파이썬 3 는 한글 식별자를 받거든요:

```
func 최댓값(점수들) {                        │  def 최댓값(점수들):
    let 최대 = 점수들[1]                     │      최대 = 점수들[0]
    for 점수 in 점수들 {                     │      for 점수 in 점수들:
        if 점수 > 최대 then { 최대 = 점수 }  │          if 점수 > 최대:
    }                                        │              최대 = 점수
    return 최대                              │      return 최대
}                                            │
                                             │
let 우리반 = [88, 94, 71]                    │  우리반 = [88, 94, 71]
print "가장 높은 점수: {최댓값(우리반)}"      │  print(f"가장 높은 점수: {최댓값(우리반)}")
```

둘 다 `가장 높은 점수: 94` 를 찍습니다. 문자열 보간은 f-string 이 되고, `then` 과 중괄호는
사라지고, 1부터 세는 인덱스는 0부터로 옮겨집니다 — 그 차이는 숨기지 않고 만들어진 파일 맨 위에
주석으로 적어 둡니다. 감출 게 아니라 배울 거니까요.

플레이그라운드에서는 클릭 한 번입니다: **🐍 Python**.

## 플레이그라운드

[웹 플레이그라운드](https://vpdrla.github.io/Venos/)는 WebAssembly로 인터프리터 전체를 브라우저에서 실행합니다 —
클래스, try/catch, 예제 RPG까지 전부 (input은 다이얼로그로 표시; `import`는 데스크톱 전용,
파일 입출력은 메모리에만 저장되어 새로고침 시 사라짐).

설치가 막힌 교실에서도 쓸 수 있게 만들었습니다:

- **🔗 Share** — 내 프로그램을 링크로 만듭니다. 선생님은 시작 코드를 나눠주고, 학생은 결과를 제출할 수 있어요
- **작업 자동 저장** — 탭을 닫아도 사라지지 않습니다
- **📚 Lessons** — 처음부터 한 단계씩 따라가는 레슨. 레슨마다 고유 링크가 있어요 (`#lesson=lists`)
- **🐍 Python** — 같은 프로그램을 파이썬으로 보여주고 복사해 줍니다. 레슨 하나가 진짜 파이썬 파일로 끝나요

## 배우기

**[TUTORIAL.ko.md](TUTORIAL.ko.md)** — `print` 부터 숫자 맞히기 게임까지 12단계. 각 레슨은 클릭 한 번으로
플레이그라운드에서 바로 실행됩니다. 영어판: [TUTORIAL.md](TUTORIAL.md).

## 설치

[최신 릴리스](https://github.com/Vpdrla/Venos/releases/latest)에서 내 컴퓨터에 맞는 파일을 받으면 끝입니다.
정적 링크라 따로 설치할 게 없어요.

| 내 컴퓨터 | 받을 파일 |
|---|---|
| Windows | `venos-windows-x64.exe` |
| macOS (인텔·애플실리콘 공용) | `venos-macos-universal` |
| Linux | `venos-linux-x64` |

macOS·Linux에서는 실행 권한을 먼저 주세요: `chmod +x venos-linux-x64`

### 소스에서 빌드하려면

```bash
g++ -std=c++17 -O2 -o venos venos.cpp        # Linux / macOS / WSL
g++ -std=c++17 -O2 -o venos.exe venos.cpp    # Windows (MinGW)
```

의존성 없음. C++17 필요 (C++20 호환). GCC·Clang·MinGW 모두에서 경고 없이 빌드됩니다.

## 사용법

```bash
./venos                      # 대화형 셸 (create / code / run / build ...)
./venos 파일.my              # 파일 바로 실행 (인터프리터)
./venos build 파일.my        # 네이티브 실행 파일로 컴파일 (g++ 설치 필요)
./venos build 파일.my run    # 컴파일 후 즉시 실행
./venos topython 파일.my     # 같은 프로그램의 파이썬 버전 만들기 (파일.py)
```

인터프리터는 그 자체로 완결돼 있고, `build` 명령만 `g++` 를 부릅니다.

셸에서 `repl` 을 입력하면 한 줄씩 실행하는 REPL이 시작됩니다 (식을 입력하면 값을 바로 표시).

전체 언어 명세: **[VENOS_SPEC.md](VENOS_SPEC.md)** (한국어) / **[VENOS_SPEC.en.md](VENOS_SPEC.en.md)** (English).
명세는 AI에게 그대로 건네주면 올바른 Venos 코드를 짜줄 수 있게 작성되어 있습니다 (AI 바이브 코딩을 염두에 둔 설계).

## 에디터 지원

`vscode-venos/` 폴더에 `.my` 파일 문법 강조를 지원하는 VS Code 확장이 들어 있습니다 —
폴더를 `~/.vscode/extensions/` 에 복사하면 됩니다 (폴더 안 README 참고).

## 예제

**[`examples/algorithms/`](examples/algorithms/)** — 교과서 알고리즘 15개 (선택 정렬, 이진 탐색,
유클리드 호제법, 에라토스테네스의 체, 하노이의 탑, 시저 암호…). 교과서가 인쇄하는 의사코드와
줄이 1:1 로 맞게 썼고, 그 의사코드를 각 파일 맨 위 주석에 그대로 넣어 두었다. 15개 전부를
테스트 스위트가 **세 방식**(인터프리터 / 빌드본 / `topython` 파이썬)으로 돌려 출력이 같은지
검사한다.

```bash
venos examples/algorithms/binary-search.my
venos topython examples/algorithms/binary-search.my    # 같은 프로그램의 파이썬 버전
```

클래스, 딕셔너리, 문자열 보간, 파일 입출력 세이브/로드를 모두 활용하는 텍스트 RPG — 두 버전:
`examples/rpg.my` (한국어 식별자) / `examples/rpg.en.my` (영어 식별자, 위 데모에 나오는 버전):

```bash
venos examples/rpg.my
venos examples/rpg.en.my
```

## 아키텍처

```
소스 → 렉서 (토큰) → 재귀 하강 파서 (AST) ─┬→ 트리워킹 인터프리터
                                          └→ C++ 코드 생성 → g++ → 네이티브 실행 파일
```

렉서, 파서, AST, 인터프리터, 트랜스파일러, 런타임 라이브러리, CLI 셸 — 전부 단일 파일 `venos.cpp` 안에 있습니다.

## 만든 이유

**Venos 가 어디에 있어야 하고 무엇이 되기를 거부하는지: [STRATEGY.ko.md](STRATEGY.ko.md)**
([English](STRATEGY.md)) — 다른 교육용 언어들이 무엇을 맞히고 무엇을 놓쳤는지, 그리고
이 언어가 노리는 빈자리.

프로그래밍 언어가 실제로 어떻게 동작하는지 이해하고 싶어서 바닥부터 만들었습니다.
변수와 `print`만 되는 v0.1 인터프리터에서 시작해, 실제 프로그램을 짜보고 부족한 걸
찾아 추가하는 방식으로 v0.6까지 왔습니다 — `examples/`의 텍스트 RPG가 `exists()`,
`try/catch`, `import` 같은 기능을 이끌어낸 검증 프로젝트입니다.

## 테스트

모든 언어 기능은 **differential testing**으로 검증합니다: `tests/cases/`의 각 프로그램을
세 백엔드(인터프리터, 트랜스파일된 네이티브 바이너리, `topython` 이 만든 파이썬)로 실행해
출력이 일치해야 합니다.
CI가 푸시마다 자동 실행:

```bash
tests/run_tests.sh
```

## 라이선스

[MIT](LICENSE)
