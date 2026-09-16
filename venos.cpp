// ============================================================
//  venos.cpp — Venos v0.6.0
//  인터프리터 + C++ 트랜스파일러 + CLI 셸 + REPL + WASM
//
//  빌드:  g++ -std=c++17 -O2 -o venos venos.cpp   (C++20도 OK)
//  언어 명세: VENOS_SPEC.md / VENOS_SPEC.en.md
// ============================================================

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <random>
#include <cmath>
#include <algorithm>
#include <set>
#include <cstdio>
#include <cstdlib>   // strtod (파이썬 숫자 리터럴을 최단 표기로 낼 때)
#include <chrono>
#include <functional>
#include <stdexcept>
#include <filesystem>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX          // windows.h 의 min/max 매크로가 std::min/max 를 깨는 것 방지
#endif
#include <windows.h>
#include <shellapi.h>   // CommandLineToArgvW — argv 를 UTF-8 로 되살리는 데 쓴다
#include <process.h>   // _beginthreadex (실행 스레드 스택 크기 지정용)
#include <conio.h>     // _getch (방향키 스크롤용)
#include <io.h>        // _isatty
#undef IN
#undef OUT             // winnt.h 의 빈 매크로 (옛 SAL 어노테이션) 제거
#elif defined(VENOS_WASM)
#include <emscripten.h>
#else
#include <pthread.h>
#include <termios.h>   // raw 키 입력 (방향키 스크롤용)
#include <unistd.h>
#endif

#ifdef VENOS_WASM
// 브라우저의 prompt() 다이얼로그로 입력 받기
// ASYNCIFY 로 빌드하므로 여기서 await 할 수 있다 — 그래야 입력을 기다리는 동안
// 브라우저가 화면을 그린다. 예전엔 동기 실행이라 출력이 DOM 에만 들어가고 칠해지지 않은 채
// prompt() 가 떠서, 학생이 "무슨 질문인지" 볼 수가 없었다.
EM_ASYNC_JS(char*, js_prompt_raw, (const char* p), {
    // UTF8ToString(p) 를 그냥 쓰면 안 된다. emscripten 은 16바이트가 넘는 문자열만
    // TextDecoder.decode(HEAPU8.subarray(...)) 로 처리하는데, 최신 Chrome 은 성장 가능한
    // wasm 힙을 resizable ArrayBuffer 로 주고 TextDecoder 는 그런 버퍼를 거부한다
    // ("The provided ArrayBuffer value must not be resizable"). 긴 프롬프트가 전부 여기서 죽었다.
    // slice() 는 항상 새 비-resizable 버퍼를 만드므로, 사본을 떠서 읽으면 어느 브라우저에서도 안전하다.
    var end = p;
    while (HEAPU8[end]) ++end;
    var msg = new TextDecoder().decode(HEAPU8.slice(p, end));
    // 페이지가 입력줄을 제공하면 그걸 쓰고, 없으면(다른 데 끼워 쓸 때) 예전처럼 prompt()
    var ask = Module.venosAskInput
           || function (m) { return Promise.resolve(prompt(m.length ? m : "input:")); };
    var r = await ask(msg);
    if (r === null || r === undefined) r = "";
    var len = lengthBytesUTF8(r) + 1;
    var buf = _malloc(len);
    stringToUTF8(r, buf, len);
    return buf;
});
static std::string g_pendingPrompt;   // input 의 프롬프트 문구를 다이얼로그로 전달
#endif

namespace fs = std::filesystem;
using std::string;

// ============================================================
//  ★ 키워드 테이블 — 여기만 바꾸면 문법 단어가 바뀜
// ============================================================
static const string KW_LET      = "let";
static const string KW_PRINT    = "print";
static const string KW_IF       = "if";
static const string KW_ELSE     = "else";
static const string KW_WHILE    = "while";
static const string KW_THEN     = "then";     // (선택) if x > 5 then { }
static const string KW_DURING   = "do";       // (선택) while x > 0 do { }
static const string KW_INPUT    = "input";
static const string KW_AND      = "and";
static const string KW_OR       = "or";
static const string KW_NOT      = "not";
static const string KW_FOR      = "for";
static const string KW_TO       = "to";
static const string KW_STEP     = "step";
static const string KW_BREAK    = "break";
static const string KW_CONTINUE = "continue";
static const string KW_FUNC     = "func";
static const string KW_RETURN   = "return";
static const string KW_IN       = "in";       // for x in xs
static const string KW_CLASS    = "class";
static const string KW_TRY      = "try";
static const string KW_CATCH    = "catch";
static const string KW_TRUE     = "true";
static const string KW_FALSE    = "false";
static const string FILE_EXT    = ".my";

static const char* VENOS_VERSION = "0.6.0";   // 릴리스 태그를 올릴 때 같이 고칠 것
// 함수 재귀 깊이 제한.
// 브라우저는 네이티브보다 호출 스택이 훨씬 얕다 — 2000 을 그대로 두면 한도에 닿기 전에
// V8 스택이 먼저 터져서 학생에게 "RangeError: Maximum call stack size exceeded" 라는
// 알아볼 수 없는 메시지가 뜬다 (실측: 브라우저는 600~800 사이에서 넘어간다).
// 웹에서는 낮춰 잡아 Venos 자신의 한국어 메시지가 먼저 나오게 한다.
// 200 으로 잡은 이유: 브라우저에서 넘어가는 지점이 실행마다 다르다. 실측에서 450 이 두 번
// 통과하고 380 이 한 번 실패했다 — V8 의 여유 스택은 그때그때 다르고, 한 번 넘치면 그 페이지에서
// 회복되지도 않는다. 넘치면 학생이 보는 건 Venos 메시지가 아니라 영어 RangeError 이므로
// 여유를 크게 둔다. 교과서 재귀(하노이 20단, 피보나치, 유클리드, 이진탐색)는 전부 깊이 수십이다.
static const int RECURSION_DESKTOP = 2000;
#ifdef VENOS_WASM
static const int MAX_RECURSION = 200;
#else
static const int MAX_RECURSION = RECURSION_DESKTOP;
#endif

struct ExitSignal {};   // exit() — 프로그램 전체를 즉시 끝내므로 예외 그대로

#ifdef VENOS_WASM
// 페이지의 "⏹ 중단" 이 입력 대신 돌려주는 값. 제어문자로 시작해 학생이 칠 수 없다.
static const char* VENOS_STOP = "\x01venos-stop";
static bool g_stopped = false;        // 중단으로 끝났는가 (끝맺음 문구를 바꾸려고)

// 계산만 도는 루프에서도 빠져나갈 수 있어야 한다. 브라우저는 한 가닥이라 wasm 이
// 붙잡고 있는 동안은 ⏹ 버튼의 클릭조차 처리되지 않는다 — 학생이 실수로
// for i = 1 to 100000000 을 돌리면 탭이 통째로 얼었다 (입력을 기다릴 때만 멈출 수 있었다).
// 그래서 루프와 함수 호출이 주기적으로 한 턴을 브라우저에 돌려준다. ASYNCIFY 덕에
// wasm 한가운데서 양보할 수 있고, 양보하는 김에 화면도 칠해지므로 **긴 프로그램의 출력이
// 끝날 때까지 한 줄도 안 보이던 것**도 같이 풀린다.
EM_JS(int, js_stop_requested, (), { return Module.venosStopRequested ? 1 : 0; });
static unsigned long g_pumpTick = 0;
static double g_pumpLast = 0;
static void pumpWeb() {
    // 값싼 쪽부터: 1024번에 한 번만 시계를 보고, 실제 양보는 100ms에 한 번.
    // (양보 한 번은 이벤트 루프 한 바퀴라 수 ms 씩 드는 반면, 카운터 증가는 공짜에 가깝다)
    if ((++g_pumpTick & 0x3FF) != 0) return;
    double now = emscripten_get_now();
    if (now - g_pumpLast < 100) return;
    g_pumpLast = now;
    emscripten_sleep(0);
    // exit() 와 같은 길로 끝낸다 — 출력 버퍼와 호출 프레임이 그래야 정리된다
    if (js_stop_requested()) { g_stopped = true; throw ExitSignal{}; }
}
#else
static inline void pumpWeb() {}
#endif

// ============================================================
//  플랫폼 헬퍼 — Windows 한글 입력/파일명 깨짐 방지
// ============================================================
static bool readLine(string& out) {
#ifdef VENOS_WASM
    char* r = js_prompt_raw(g_pendingPrompt.c_str());
    out = r;
    free(r);
    g_pendingPrompt.clear();
    // 입력을 기다리는 동안 학생이 빠져나갈 길이 필요하다 — 숫자 맞히기처럼
    // while true 로 도는 프로그램은 그러지 않으면 새로고침 말고는 방법이 없다.
    // exit() 와 같은 길(ExitSignal)로 끝내야 출력 버퍼와 호출 프레임이 정리된다.
    if (out == VENOS_STOP) { g_stopped = true; throw ExitSignal{}; }
    std::cout << out << "\n";   // 입력값을 출력창에도 기록
    return true;
#endif
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;
    if (GetConsoleMode(h, &mode)) {  // 진짜 콘솔 입력일 때만 (파이프면 아래 getline)
        wchar_t wbuf[4096];
        DWORD nRead = 0;
        if (!ReadConsoleW(h, wbuf, 4096, &nRead, nullptr)) return false;
        std::wstring ws(wbuf, nRead);
        while (!ws.empty() && (ws.back() == L'\n' || ws.back() == L'\r')) ws.pop_back();
        int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(),
                                      nullptr, 0, nullptr, nullptr);
        out.assign(len, '\0');
        WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(),
                            out.data(), len, nullptr, nullptr);
        return true;
    }
#endif
    return (bool)std::getline(std::cin, out);
}

// 문자열이 "학생이 숫자라고 생각하는 것"인가.
//
// std::stod 를 그냥 쓰면 안 된다 — C++ 는 `0x10`(16), `inf`, `nan` 까지 받아 주는데
// 파이썬의 float() 은 셋 다 거절한다. 반대로 파이썬은 `1_000` 을 1000 으로 받는다.
// 어느 쪽이든 같은 프로그램이 백엔드에 따라 다른 답을 낸다 (실제로 그랬다).
// 그래서 세 백엔드가 공통으로 이 문법만 받는다:
//     [공백] [+-] ( 숫자+ [ . 숫자* ] | . 숫자+ ) ( [eE] [+-] 숫자+ )? [공백]
// 여기에 무한대·NaN 은 결과에서 걸러낸다 (1e999 는 어차피 stod 가 던진다).
static bool strToNum(const string& s, double& out) {
    size_t i = 0, n = s.size();
    auto skipSpace = [&] {
        while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n'
                         || s[i] == '\r' || s[i] == '\v' || s[i] == '\f')) i++;
    };
    auto digits = [&] {
        size_t k = 0;
        while (i < n && s[i] >= '0' && s[i] <= '9') { i++; k++; }
        return k;
    };
    skipSpace();
    if (i < n && (s[i] == '+' || s[i] == '-')) i++;
    size_t whole = digits(), frac = 0;
    if (i < n && s[i] == '.') { i++; frac = digits(); }
    if (whole == 0 && frac == 0) return false;
    if (i < n && (s[i] == 'e' || s[i] == 'E')) {
        i++;
        if (i < n && (s[i] == '+' || s[i] == '-')) i++;
        if (digits() == 0) return false;          // "1e" 는 숫자가 아니다
    }
    skipSpace();
    if (i != n) return false;
    try { out = std::stod(s); } catch (...) { return false; }
    return out == out && out < HUGE_VAL && out > -HUGE_VAL;   // inf/nan 거르기
}

// UTF-8 문자열 → 파일 경로 (Windows는 와이드 변환 필수)
static fs::path toPath(const string& utf8) {
#ifdef _WIN32
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), nullptr, 0);
    std::wstring w(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), w.data(), len);
    return fs::path(w);
#else
    return fs::path(utf8);
#endif
}

[[maybe_unused]] static bool hasNonAscii(const string& s) {   // 윈도우의 g++ 호출에서만 쓴다
    for (unsigned char c : s) if (c >= 0x80) return true;
    return false;
}

// 셸 명령 실행. 윈도우의 system() 은 명령줄을 ANSI 코드페이지로 넘기므로 한글 경로가
// 뭉개진다 — 만든 exe 를 "venos build 숙제/정렬.my run" 으로 바로 돌릴 때 걸렸다.
// _wsystem 은 넓은 명령줄을 그대로 넘기고 cmd.exe 는 유니코드를 온전히 다룬다.
static int runShell(const string& cmd) {
    std::cout << std::flush;
#ifdef _WIN32
    int len = MultiByteToWideChar(CP_UTF8, 0, cmd.c_str(), (int)cmd.size(), nullptr, 0);
    std::wstring w(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, cmd.c_str(), (int)cmd.size(), w.data(), len);
    return _wsystem(w.c_str());
#else
    return std::system(cmd.c_str());
#endif
}

// \033[2J: 보이는 화면 지우기, \033[3J: 스크롤백(위로 올린 기록)까지 지우기, \033[H: 커서 맨 위로
static void clearScreen() { std::cout << "\033[2J\033[3J\033[H" << std::flush; }

static void drawBanner() {
    std::cout << "==========================================\n";
    std::cout << "  Venos Shell v" << VENOS_VERSION << "  (help 로 도움말)\n";
    std::cout << "==========================================\n";
}

static string trim(const string& s) {
    size_t a = s.find_first_not_of(" \t\r");
    if (a == string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r");
    return s.substr(a, b - a + 1);
}

// ---- 방향키 스크롤용 raw 키 입력 ----
enum Key { K_UP = 1000, K_DOWN, K_PGUP, K_PGDN, K_HOME, K_END, K_QUIT, K_OTHER };
static int readKey() {
#if defined(VENOS_WASM)
    return K_QUIT;   // 웹에선 셸 뷰어를 쓰지 않음
#elif defined(_WIN32)
    if (!_isatty(0)) {   // 파이프 입력이면 (테스트용) 한 줄 명령으로 대체
        string l; if (!std::getline(std::cin, l)) return K_QUIT;
        l = trim(l);
        if (l == "u") return K_UP;
        if (l == "d") return K_DOWN;
        if (l == "U") return K_PGUP;
        if (l == "D") return K_PGDN;
        return K_QUIT;
    }
    int c = _getch();
    if (c == 0 || c == 224) {          // 확장 키 (방향키 등)
        int c2 = _getch();
        switch (c2) {
            case 72: return K_UP;   case 80: return K_DOWN;
            case 73: return K_PGUP; case 81: return K_PGDN;
            case 71: return K_HOME; case 79: return K_END;
        }
        return K_OTHER;
    }
    if (c == 'q' || c == 'Q' || c == 27) return K_QUIT;
    return K_OTHER;
#else
    if (!isatty(0)) {
        string l; if (!std::getline(std::cin, l)) return K_QUIT;
        l = trim(l);
        if (l == "u") return K_UP;
        if (l == "d") return K_DOWN;
        if (l == "U") return K_PGUP;
        if (l == "D") return K_PGDN;
        return K_QUIT;
    }
    termios oldT, rawT;
    tcgetattr(0, &oldT);
    rawT = oldT;
    rawT.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(0, TCSANOW, &rawT);
    int result = K_OTHER;
    unsigned char c = 0;
    if (read(0, &c, 1) == 1) {
        if (c == 27) {                 // ESC 시퀀스 (방향키)
            unsigned char a = 0, b = 0;
            if (read(0, &a, 1) == 1 && a == '[' && read(0, &b, 1) == 1) {
                switch (b) {
                    case 'A': result = K_UP;   break;
                    case 'B': result = K_DOWN; break;
                    case 'H': result = K_HOME; break;
                    case 'F': result = K_END;  break;
                    case '5': { unsigned char t; (void)!read(0, &t, 1); result = K_PGUP; } break;
                    case '6': { unsigned char t; (void)!read(0, &t, 1); result = K_PGDN; } break;
                }
            } else result = K_QUIT;    // ESC 단독
        }
        else if (c == 'q' || c == 'Q') result = K_QUIT;
    } else result = K_QUIT;
    tcsetattr(0, TCSANOW, &oldT);
    return result;
#endif
}

// 방향키 스크롤 뷰어 — 코드 전체를 위아래로 훑어보기
static void scrollViewer(const std::vector<string>& lines, const string& title) {
    const int H = 18;                                    // 한 화면에 보일 줄 수
    int off = 0;
    int maxOff = std::max(0, (int)lines.size() - H);
    while (true) {
        clearScreen();
        std::cout << "── " << title << " (" << lines.size()
                  << "줄)  ↑↓ 한 줄 · PgUp/PgDn 한 화면 · Home/End · q 나가기 ──\n";
        if (off > 0) std::cout << "  … (위로 " << off << "줄 더)\n";
        int last = std::min((int)lines.size(), off + H);
        for (int i = off; i < last; i++)
            std::cout << "  " << (i + 1) << " | " << lines[i] << "\n";
        if (lines.empty()) std::cout << "  (빈 파일)\n";
        if (last < (int)lines.size())
            std::cout << "  … (아래로 " << (int)lines.size() - last << "줄 더)\n";
        std::cout << std::flush;
        switch (readKey()) {
            case K_UP:   off = std::max(0, off - 1);        break;
            case K_DOWN: off = std::min(maxOff, off + 1);   break;
            case K_PGUP: off = std::max(0, off - H);        break;
            case K_PGDN: off = std::min(maxOff, off + H);   break;
            case K_HOME: off = 0;                           break;
            case K_END:  off = maxOff;                      break;
            case K_QUIT: return;
            default: break;
        }
    }
}

// UTF-8 문자 수 세기 (바이트 수 아님 — 한글도 1글자로)
static size_t utf8Length(const string& s) {
    size_t n = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) n++;
    return n;
}

// UTF-8 문자열을 "글자" 단위로 쪼개기 (한글 = 1글자)
static std::vector<string> utf8Chars(const string& s) {
    std::vector<string> out;
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = s[i];
        size_t len = 1;
        if      ((c & 0x80) == 0x00) len = 1;
        else if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        if (i + len > s.size()) len = 1;   // 깨진 인코딩 방어
        out.push_back(s.substr(i, len));
        i += len;
    }
    return out;
}

// 아주 긴 문자열을 에러 메시지에 그대로 끼워 넣으면 정작 읽어야 할 설명이
// 화면 밖으로 밀려난다 (괄호를 수천 개 친 입력을 퍼저가 만들어 냈다).
static string ellipsize(const string& s, size_t limit) {
    if (utf8Length(s) <= limit) return s;
    auto chars = utf8Chars(s);
    string out;
    for (size_t i = 0; i < limit; i++) out += chars[i];
    return out + " …";
}

// ---- 한국어 조사 ----
// 앞 글자에 받침이 있으면 첫 번째, 없으면 두 번째를 쓴다.
// "문자열와(과) 숫자는" 처럼 나가면 교육용 언어의 에러 메시지로는 어색하다.
// 한글이 아니면 (기호·영어 이름) 고를 근거가 없으니 지금처럼 둘 다 보여 준다.
static string josa(const string& word, const char* withJong, const char* without) {
    auto cs = utf8Chars(word);
    if (!cs.empty() && cs.back().size() == 3) {
        const string& last = cs.back();
        unsigned cp = ((unsigned char)last[0] & 0x0Fu) << 12
                    | ((unsigned char)last[1] & 0x3Fu) << 6
                    | ((unsigned char)last[2] & 0x3Fu);
        if (cp >= 0xAC00 && cp <= 0xD7A3)        // 한글 음절
            return ((cp - 0xAC00) % 28) ? withJong : without;
    }
    return " " + string(withJong) + "(" + without + ")";   // 기호·영어는 띄어서 둘 다
}

// ---- 오타 제안 ----
// "정의되지 않은 변수: 이릅" 만 던지고 끝내면 초보자는 뭐가 틀렸는지 못 찾는다.
// 편집 거리를 글자 단위로 재서 (바이트로 재면 한글이 전부 거리 3 이 된다) 가까운 이름을 붙여 준다.
static size_t editDistance(const std::vector<string>& a, const std::vector<string>& b) {
    std::vector<size_t> prev(b.size() + 1), cur(b.size() + 1);
    for (size_t j = 0; j <= b.size(); j++) prev[j] = j;
    for (size_t i = 1; i <= a.size(); i++) {
        cur[0] = i;
        for (size_t j = 1; j <= b.size(); j++)
            cur[j] = std::min({ prev[j] + 1, cur[j - 1] + 1,
                                prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1) });
        prev = cur;
    }
    return prev[b.size()];
}
// 후보 중 가장 가까운 이름을 "  (혹시 'X'?)" 로. 마땅한 게 없으면 빈 문자열.
static string foreignHint(const string& name);
static string suggestName(const string& typo, std::vector<string> cands) {
    string foreign = foreignHint(typo);
    if (!foreign.empty()) return foreign;            // 오타가 아니라 다른 언어의 이름이다
    auto t = utf8Chars(typo);
    if (t.size() < 2) return "";                       // 한 글자짜리는 아무거나 다 가까워진다
    size_t maxD = t.size() <= 4 ? 1 : 2;
    std::sort(cands.begin(), cands.end());
    cands.erase(std::unique(cands.begin(), cands.end()), cands.end());
    string best;
    size_t bestD = maxD + 1;
    for (const string& c : cands) {
        if (c == typo) continue;
        auto cc = utf8Chars(c);
        if (cc.size() + maxD < t.size() || t.size() + maxD < cc.size()) continue;
        size_t d = editDistance(t, cc);
        if (d < bestD) { bestD = d; best = c; }
    }
    return best.empty() ? "" : "  (혹시 '" + best + "'?)";
}

// "abc".upper() 처럼 원시값에 메서드를 부른 경우의 안내. 인터프리터와 생성 코드가
// 같은 문구를 쓰도록 여기 한 군데에 둔다 (정의는 BUILTIN_NAMES 뒤).
static string methodHint(const string& method);

// 다른 언어의 이름을 그대로 쓴 것 — 오타가 아니라 "그 언어의 버릇"이다.
// Venos 의 학생은 블록 코딩에서 오고 파이썬으로 가므로, 파이썬 습관이 가장 흔하다.
// "정의되지 않은 변수: True" 로 끝내면 소문자 true 가 있다는 걸 알 길이 없다.
static const std::map<string, string>& foreignNames() {
    static const std::map<string, string> M = {
        {"True",    "참은 소문자 true 입니다"},
        {"False",   "거짓은 소문자 false 입니다"},
        {"None",    "Venos 에는 None 이 없습니다 — 빈 값은 0 이나 \"\" 로 나타내세요"},
        {"null",    "Venos 에는 null 이 없습니다 — 빈 값은 0 이나 \"\" 로 나타내세요"},
        {"nil",     "Venos 에는 nil 이 없습니다 — 빈 값은 0 이나 \"\" 로 나타내세요"},
        {"undefined","Venos 에는 undefined 가 없습니다 — 빈 값은 0 이나 \"\" 로 나타내세요"},
        {"this",    "객체 자신은 self 입니다"},
        {"elif",    "elif 는 없습니다 — else if 로 쓰세요"},
        {"elseif",  "elseif 는 없습니다 — else if 로 쓰세요"},
        {"elsif",   "elsif 는 없습니다 — else if 로 쓰세요"},
        {"def",     "함수는 def 가 아니라 func 로 만듭니다"},
        {"function","함수는 function 이 아니라 func 로 만듭니다"},
        {"lambda",  "Venos 에는 이름 없는 함수가 없습니다 — func 로 이름을 붙이세요"},
        {"var",     "변수는 var 가 아니라 let 으로 만듭니다"},
        {"const",   "변수는 const 가 아니라 let 으로 만듭니다"},
        {"println", "출력은 print 입니다"},
        {"printf",  "출력은 print 입니다 (자리표시자 대신 \"값: {x}\" 처럼 씁니다)"},
        {"echo",    "출력은 print 입니다"},
        {"cout",    "출력은 print 입니다"},
        {"length",  "길이는 length 가 아니라 len(x) 입니다"},
        {"size",    "길이는 size 가 아니라 len(x) 입니다"},
        {"strlen",  "길이는 strlen 이 아니라 len(x) 입니다"},
        {"append",  "리스트에 붙일 때는 push(리스트, 값) 입니다"},
        {"range",   "Venos 에는 range 가 없습니다 — for i = 1 to n 으로 씁니다"},
        {"pass",    "Venos 에는 pass 가 없습니다 — 아무것도 안 하려면 { } 를 비워 두세요"},
        {"raise",   "에러를 일으키려면 error(\"메시지\") 입니다"},
        {"throw",   "에러를 일으키려면 error(\"메시지\") 입니다"},
        {"except",  "try 뒤에는 except 가 아니라 catch 를 씁니다"},
        {"end",     "블록은 end 가 아니라 } 로 닫습니다"},
        {"new",     "객체는 new 없이 클래스이름(...) 으로 만듭니다"},
    };
    return M;
}
// 위 목록에 있으면 "  (설명)" 을, 없으면 빈 문자열을 준다.
static string foreignHint(const string& name) {
    auto it = foreignNames().find(name);
    return it == foreignNames().end() ? "" : "  (" + it->second + ")";
}
// 내장 함수 이름 — 오타 제안 후보로 쓴다 (인터프리터·트랜스파일러가 같이 본다)
static const std::vector<string> BUILTIN_NAMES = {
    "random", "round", "floor", "ceil", "abs", "sqrt", "min", "max", "num", "str",
    "len", "push", "pop", "sort", "reverse", "remove", "keys", "has",
    "split", "join", "upper", "lower", "find", "replace", "substr",
    "readfile", "writefile", "appendfile", "exists", "time", "exit", "copy", "error",
};
// 내장 함수와 같은 이름으로 함수·클래스를 만들면 백엔드마다 답이 달랐다:
// 인터프리터는 내장을 썼고, build 는 거절했고, 파이썬은 학생의 함수를 썼다.
// 파서에서 한 번 막으면 세 곳이 같아진다 (메서드 이름은 obj.len() 으로 구분되므로 제외).
static bool isBuiltinName(const string& name) {
    return std::find(BUILTIN_NAMES.begin(), BUILTIN_NAMES.end(), name) != BUILTIN_NAMES.end();
}
static string methodHint(const string& method) {
    if (std::find(BUILTIN_NAMES.begin(), BUILTIN_NAMES.end(), method) != BUILTIN_NAMES.end())
        return "  (" + method + "(x) 처럼 앞에 붙여 쓰세요)";
    return foreignHint(method);
}

// ============================================================
//  1. 렉서 (Lexer)
// ============================================================
enum class Tok {
    LET, PRINT, IF, ELSE, WHILE, FILLER,
    INPUT, AND, OR, NOT,
    FOR, TO, STEP, BREAK, CONTINUE, FUNC, RETURN, INKW, CLASS, DOT, TRY, CATCH,  // INKW: windows.h 가 IN 을 매크로로 정의해서 회피
    IDENT, NUMBER, STRING,
    PLUS, MINUS, STAR, SLASH, PERCENT,
    ASSIGN, PLUSEQ, MINUSEQ, STAREQ, SLASHEQ,
    EQ, NEQ, LT, GT, LE, GE,
    LPAREN, RPAREN, LBRACE, RBRACE, LBRACKET, RBRACKET, COMMA, COLON,
    END
};

struct Token {
    Tok type;
    string text;
    double num = 0;
    int line = 0;
};

// ---- 호출 경로 ----
// 에러가 함수 안쪽에서 나면 "어디서 불렀는지"가 사실상 답이다. 파이썬은 traceback 을
// 통째로 보여 주는데 그건 초보자에게 겁만 주므로, 여기서는 한 줄로 줄여서 보여 준다.
// 에러가 만들어지는 순간의 스택을 찍어 둔다 (던진 뒤엔 DepthGuard 가 이미 걷혀 있다).
struct CallFrame { const string* name; int line; };   // 이름은 AST 가 들고 있으므로 포인터로
static std::vector<CallFrame> g_frames;
static string callPath();

// 에러 문구에는 식별자나 문자열이 그대로 들어간다. 이름이 3천 자짜리면 (퍼저가
// 만들어 낸다) 9KB 짜리 에러 한 줄이 나와 읽을 게 없어진다. 실제로 쓰는 문구 중
// 가장 긴 것이 100자 남짓이라, 한 군데서 넉넉히 잘라 두면 모든 자리가 같이 안전하다.
struct LangError : std::runtime_error {
    string path;                       // "바깥(줄 10) → 가운데(줄 8)" — 없으면 빈 문자열
    LangError(const string& msg) : std::runtime_error(ellipsize(msg, 300)), path(callPath()) {}
};

// import 로 파일이 병합되면, 병합된 줄번호 → "원본파일 줄 N" 매핑을 채운다.
// 비어 있으면(단일 파일) 그냥 "줄 N" 으로 표시.
static std::vector<string> g_lineMap;
static string lineTag(int line) {
    if (line >= 1 && line < (int)g_lineMap.size() && !g_lineMap[line].empty())
        return "[" + g_lineMap[line] + "] ";
    return "[줄 " + std::to_string(line) + "] ";
}
// 지금 쌓여 있는 호출을 한 줄로. 깊은 재귀는 가운데를 접는다 (2000줄을 쏟으면 안 되므로).
static string callPath() {
    if (g_frames.empty()) return "";
    auto one = [](const CallFrame& f) {
        string tag = lineTag(f.line);
        if (!tag.empty() && tag.front() == '[') tag = tag.substr(1, tag.find(']') - 1);
        return *f.name + " (" + tag + "에서)";
    };
    const size_t HEAD = 3, TAIL = 2;
    string out;
    if (g_frames.size() <= HEAD + TAIL + 1) {
        for (size_t i = 0; i < g_frames.size(); i++)
            out += (i ? " → " : "") + one(g_frames[i]);
        return out;
    }
    for (size_t i = 0; i < HEAD; i++) out += (i ? " → " : "") + one(g_frames[i]);
    out += " → ... " + std::to_string(g_frames.size() - HEAD - TAIL) + "개 더 → ";
    for (size_t i = g_frames.size() - TAIL; i < g_frames.size(); i++)
        out += one(g_frames[i]) + (i + 1 < g_frames.size() ? " → " : "");
    return out;
}

// 마지막으로 실행/빌드한 소스 (에러 시 해당 줄을 보여주기 위해 보관)
static std::vector<string> g_srcLines;

// 에러 메시지 출력 + 문제의 코드 줄 표시
//   !! 에러: [줄 12] 키가 없습니다: "점수"
//       줄 12 | print d["점수"]
static void printError(const string& msg, const string& prefix = "!! 에러: ") {
    std::cout << prefix << msg << "\n";
    size_t a = msg.find('[');
    size_t b = msg.find(']');
    if (a == string::npos || b == string::npos || b < a) return;
    string tag = msg.substr(a + 1, b - a - 1);
    int merged = 0;
    if (!g_lineMap.empty()) {
        // import 사용 시: "파일 줄 N" 라벨을 병합 줄번호로 역변환
        for (size_t i = 1; i < g_lineMap.size(); i++)
            if (g_lineMap[i] == tag) { merged = (int)i; break; }
    } else if (tag.rfind("줄 ", 0) == 0) {
        merged = atoi(tag.c_str() + string("줄 ").size());
    }
    if (merged >= 1 && merged <= (int)g_srcLines.size()) {
        string src = trim(g_srcLines[merged - 1]);
        const size_t LIMIT = 120;
        string shown = ellipsize(src, LIMIT);
        if (shown != src) shown += " (" + std::to_string(utf8Length(src)) + "글자)";
        std::cout << "    " << tag << " | " << shown << "\n";
    }
}
// 에러 + 그 에러가 난 자리까지 오게 된 호출 경로
static void printError(const LangError& e) {
    printError(e.what());
    if (!e.path.empty()) std::cout << "    부른 순서: " << e.path << "\n";
}

// ============================================================
//  import 전개 — 실행/빌드 전에 import "파일.my" 줄을 해당 파일
//  내용으로 치환하고, 병합 줄번호 → 원본 위치 매핑을 만든다.
//  같은 파일은 한 번만 로드 (중복/순환 import 자동 방지).
// ============================================================
// 파일 열기 — 윈도우에서는 반드시 넓은 경로로 연다.
// std::ifstream 의 filesystem::path 생성자는 MinGW 빌드에서 기대대로 동작하지 않는다:
// 트랜스파일 빌드본에서 없는 파일도 "열렸다"고 답해 exists() 와 readfile() 이 전부
// 참이 됐다 (윈도우 CI 가 잡았다). 그래서 두 백엔드 모두 이 함수 하나만 쓴다.
// 모드는 바이너리 고정 — 텍스트 모드의 CRLF 변환이 백엔드마다 다르게 걸리지 않도록.
static std::FILE* openFile(const string& path, const char* mode, const wchar_t* wmode) {
#ifdef _WIN32
    (void)mode;
    return _wfopen(toPath(path).c_str(), wmode);
#else
    (void)wmode;
    return std::fopen(path.c_str(), mode);
#endif
}
static string readAll(std::FILE* fp) {
    string out;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, fp)) > 0) out.append(buf, n);
    std::fclose(fp);
    return out;
}
static void writeAll(std::FILE* fp, const string& s) {
    if (!s.empty()) std::fwrite(s.data(), 1, s.size(), fp);
    std::fclose(fp);
}
static fs::path importToPath(const string& utf8) { return toPath(utf8); }
// "폴더/파일.my" → "폴더". UTF-8 안전하다 — / 와 \ 는 멀티바이트 글자 안에 나올 수 없다.
static string dirOf(const string& p) {
    size_t i = p.find_last_of("/\\");
    return i == string::npos ? "" : p.substr(0, i);
}
static bool isAbsPath(const string& p) {
    return !p.empty() && (p[0] == '/' || p[0] == '\\'
                          || (p.size() > 1 && p[1] == ':'));   // 윈도우 C:\...
}

// label 은 에러 메시지에 쓰는 이름 — import 에 적힌 그대로다.
// 여는 데 쓰는 path 는 "import 를 쓴 파일 옆" 기준으로 풀린 경로라 길어질 수 있는데,
// 학생에게는 자기가 적은 "utils.my" 로 보여야 찾아갈 수 있다.
static void loadWithImports(const string& rawPath, const string& rawLabel,
                            std::set<string>& loaded,
                            string& out, std::vector<string>& lmap,
                            bool& sawImport, const string& fromWhere) {
    string path = rawPath;
    string label = rawLabel;
    if (label.size() < FILE_EXT.size()
        || label.substr(label.size() - FILE_EXT.size()) != FILE_EXT)
        label += FILE_EXT;
    if (path.size() < FILE_EXT.size()
        || path.substr(path.size() - FILE_EXT.size()) != FILE_EXT)
        path += FILE_EXT;
    if (loaded.count(path)) return;      // 이미 로드됨 → 스킵
    loaded.insert(path);

    std::ifstream in(importToPath(path));
    if (!in)
        throw LangError("import 실패: 파일을 열 수 없습니다: " + label
                        + (fromWhere.empty() ? "" : "  (" + fromWhere + " 에서)"));
    string line;
    int no = 0;
    while (std::getline(in, line)) {
        no++;
        // "import \"파일\"" 형태인지 검사 (앞 공백 허용, 뒤엔 공백/#주석만)
        string t = line;
        size_t a = t.find_first_not_of(" \t\r");
        if (a != string::npos && t.compare(a, 6, "import") == 0) {
            size_t p = t.find_first_not_of(" \t", a + 6);
            if (p != string::npos && t[p] == '"') {
                size_t q = t.find('"', p + 1);
                if (q != string::npos) {
                    string rest = t.substr(q + 1);
                    size_t r = rest.find_first_not_of(" \t\r");
                    if (r == string::npos || rest[r] == '#') {
                        sawImport = true;
                        // import 는 **그 import 를 쓴 파일 옆**을 기준으로 찾는다.
                        // 현재 작업 폴더 기준이면 "venos 프로젝트/main.my" 를 프로젝트
                        // 밖에서 돌릴 때 옆에 둔 utils.my 를 못 연다.
                        string asWritten = t.substr(p + 1, q - p - 1);
                        string target = asWritten;
                        string base = dirOf(path);
                        if (!base.empty() && !isAbsPath(target)) target = base + "/" + target;
                        loadWithImports(target, asWritten, loaded, out, lmap,
                                        sawImport, label + " 줄 " + std::to_string(no));
                        continue;    // import 줄 자체는 출력에 넣지 않음
                    }
                }
            }
        }
        out += line;
        out += "\n";
        lmap.push_back(label + " 줄 " + std::to_string(no));
    }
}

static string expandImports(const string& mainPath) {
    std::set<string> loaded;
    string out;
    std::vector<string> lmap;
    lmap.push_back("");                  // 줄번호는 1부터라 0번은 비움
    bool sawImport = false;
    loadWithImports(mainPath, mainPath, loaded, out, lmap, sawImport, "");
    if (sawImport) g_lineMap = lmap;     // import 썼을 때만 "파일:줄" 표기
    else           g_lineMap.clear();
    // 에러 표시용으로 소스 줄 보관
    g_srcLines.clear();
    string cur;
    for (char c : out) {
        if (c == '\n') { g_srcLines.push_back(cur); cur.clear(); }
        else cur += c;
    }
    return out;
}

static bool isIdentChar(char c) {
    return isalnum((unsigned char)c) || c == '_' || (unsigned char)c >= 0x80;
}
static bool isIdentStart(char c) {
    return isalpha((unsigned char)c) || c == '_' || (unsigned char)c >= 0x80;
}

std::vector<Token> lex(const string& src) {
    std::vector<Token> toks;
    int line = 1;
    size_t i = 0;
    auto push = [&](Tok t, const string& s = "", double n = 0) {
        toks.push_back({t, s, n, line});
    };
    auto err = [&](const string& m) {
        return LangError(lineTag(line) + "" + m);
    };

    while (i < src.size()) {
        char c = src[i];
        if (c == '\n') { line++; i++; continue; }
        if (isspace((unsigned char)c)) { i++; continue; }
        if (c == '#') { while (i < src.size() && src[i] != '\n') i++; continue; }

        // 숫자 — 소수점은 최대 1개, 숫자 바로 뒤에 글자 금지
        if (isdigit((unsigned char)c)) {
            size_t start = i;
            int dots = 0;
            while (i < src.size() && (isdigit((unsigned char)src[i]) || src[i] == '.')) {
                if (src[i] == '.') dots++;
                i++;
            }
            string numStr = src.substr(start, i - start);
            if (dots > 1)
                throw err("잘못된 숫자: " + numStr + " (소수점은 1개만)");
            if (numStr.back() == '.')
                throw err("잘못된 숫자: " + numStr + " (소수점 뒤에 숫자가 필요)");
            if (i < src.size() && isIdentStart(src[i]))
                throw err("숫자 바로 뒤에 글자가 올 수 없습니다: " + numStr + src[i]);
            push(Tok::NUMBER, "", std::stod(numStr));
            continue;
        }
        // 문자열 — 이스케이프 지원: \n \t \" 그리고 백슬래시 2개
        if (c == '"') {
            i++;
            string s;
            while (i < src.size() && src[i] != '"') {
                if (src[i] == '\\') {
                    if (i + 1 >= src.size()) throw err("문자열이 \\ 로 끝났습니다");
                    char e = src[i + 1];
                    if      (e == 'n')  s += '\n';
                    else if (e == 't')  s += '\t';
                    else if (e == '"')  s += '"';
                    else if (e == '\\') s += '\\';
                    else throw err(string("알 수 없는 이스케이프: \\") + e + "  (\\n \\t \\\" \\\\ 만 가능)");
                    i += 2;
                    continue;
                }
                if (src[i] == '\n') line++;
                s += src[i++];
            }
            if (i >= src.size())
                throw err("문자열이 닫히지 않았습니다 (\" 누락)");
            i++;
            push(Tok::STRING, s);
            continue;
        }
        if (isIdentStart(c)) {
            size_t start = i;
            while (i < src.size() && isIdentChar(src[i])) i++;
            string w = src.substr(start, i - start);
            if      (w == KW_LET)      push(Tok::LET);
            else if (w == KW_PRINT)    push(Tok::PRINT);
            else if (w == KW_IF)       push(Tok::IF);
            else if (w == KW_ELSE)     push(Tok::ELSE);
            else if (w == KW_WHILE)    push(Tok::WHILE);
            else if (w == KW_THEN || w == KW_DURING) push(Tok::FILLER, w);
            else if (w == KW_INPUT)    push(Tok::INPUT);
            else if (w == KW_AND)      push(Tok::AND);
            else if (w == KW_OR)       push(Tok::OR);
            else if (w == KW_NOT)      push(Tok::NOT);
            else if (w == KW_FOR)      push(Tok::FOR);
            else if (w == KW_TO)       push(Tok::TO);
            else if (w == KW_STEP)     push(Tok::STEP);
            else if (w == KW_BREAK)    push(Tok::BREAK);
            else if (w == KW_CONTINUE) push(Tok::CONTINUE);
            else if (w == KW_FUNC)     push(Tok::FUNC);
            else if (w == KW_RETURN)   push(Tok::RETURN);
            else if (w == KW_IN)       push(Tok::INKW);
            else if (w == KW_CLASS)    push(Tok::CLASS);
            else if (w == KW_TRY)      push(Tok::TRY);
            else if (w == KW_CATCH)    push(Tok::CATCH);
            else if (w == KW_TRUE)     push(Tok::NUMBER, "", 1);   // true = 1
            else if (w == KW_FALSE)    push(Tok::NUMBER, "", 0);   // false = 0
            else                       push(Tok::IDENT, w);
            continue;
        }
        auto two = [&](char a, char b) {
            return src[i] == a && i + 1 < src.size() && src[i + 1] == b;
        };
        // 다른 언어의 습관으로 친 것들. 그대로 "알 수 없는 문자" 라고 하면 학생은
        // 자기가 무슨 언어의 버릇을 썼는지 모른다 — 무엇을 대신 쓰는지까지 말해 준다.
        if (two('/', '/')) throw err("주석은 // 가 아니라 # 로 씁니다"
                                     " (정수 나눗셈을 뜻한 거라면 floor(a / b))");
        if (two('/', '*')) throw err("주석은 /* */ 가 아니라 # 로 씁니다 (여러 줄이면 줄마다 #)");
        if (two('*', '*')) throw err("거듭제곱 연산자는 없습니다 — x * x 로 쓰거나 반복문으로 곱하세요");
        if (two('+', '+')) throw err("++ 는 없습니다 — x += 1 로 쓰세요");
        if (two('<', '>')) throw err("같지 않음은 <> 가 아니라 != 입니다");
        if (two('&', '&')) throw err("그리고는 && 가 아니라 " + KW_AND + " 입니다");
        if (two('|', '|')) throw err("또는은 || 가 아니라 " + KW_OR + " 입니다");
        if      (two('=', '=')) { push(Tok::EQ);      i += 2; }
        else if (two('!', '=')) { push(Tok::NEQ);     i += 2; }
        else if (two('<', '=')) { push(Tok::LE);      i += 2; }
        else if (two('>', '=')) { push(Tok::GE);      i += 2; }
        else if (two('+', '=')) { push(Tok::PLUSEQ);  i += 2; }
        else if (two('-', '=')) { push(Tok::MINUSEQ); i += 2; }
        else if (two('*', '=')) { push(Tok::STAREQ);  i += 2; }
        else if (two('/', '=')) { push(Tok::SLASHEQ); i += 2; }
        else {
            switch (c) {
                case '+': push(Tok::PLUS);     break;
                case '-': push(Tok::MINUS);    break;
                case '*': push(Tok::STAR);     break;
                case '/': push(Tok::SLASH);    break;
                case '%': push(Tok::PERCENT);  break;
                case '=': push(Tok::ASSIGN);   break;
                case '<': push(Tok::LT);       break;
                case '>': push(Tok::GT);       break;
                case '(': push(Tok::LPAREN);   break;
                case ')': push(Tok::RPAREN);   break;
                case '{': push(Tok::LBRACE);   break;
                case '}': push(Tok::RBRACE);   break;
                case '[': push(Tok::LBRACKET); break;
                case ']': push(Tok::RBRACKET); break;
                case ',': push(Tok::COMMA);    break;
                case ':': push(Tok::COLON);    break;
                case '.': push(Tok::DOT);      break;
                case '!': throw err("논리 부정은 ! 가 아니라 " + KW_NOT + " 을 씁니다"
                                    " (같지 않음은 != 로 붙여 씁니다)");
                case ';': throw err("Venos 는 문장 끝에 ; 를 붙이지 않습니다 (그냥 지우세요)");
                case '^': throw err("거듭제곱 연산자는 없습니다 — x * x 로 쓰거나 반복문으로 곱하세요");
                case '&': throw err("그리고는 & 가 아니라 " + KW_AND + " 입니다");
                case '|': throw err("또는은 | 가 아니라 " + KW_OR + " 입니다");
                default:
                    throw err(string("알 수 없는 문자: '") + c + "'");
            }
            i++;
        }
    }
    push(Tok::END);
    return toks;
}

// ============================================================
//  2. 값(Value)과 환경(Env)
// ============================================================
struct Value {
    enum Kind { NUM, STR, LIST, MAP, OBJ } kind = NUM;
    double num = 0;
    string str;
    // 리스트/딕셔너리/객체는 shared_ptr — 복사해도 같은 것을 가리킴 (Python처럼 참조 방식)
    std::shared_ptr<std::vector<Value>> list;
    std::shared_ptr<std::map<string, Value>> map;   // MAP 의 항목 / OBJ 의 필드
    string className;                                // OBJ 일 때 클래스 이름

    static Value number(double d) { Value v; v.kind = NUM; v.num = d; return v; }
    static Value text(string s)   { Value v; v.kind = STR; v.str = std::move(s); return v; }
    static Value makeList(std::vector<Value> xs) {
        Value v; v.kind = LIST;
        v.list = std::make_shared<std::vector<Value>>(std::move(xs));
        return v;
    }
    static Value makeMap() {
        Value v; v.kind = MAP;
        v.map = std::make_shared<std::map<string, Value>>();
        return v;
    }
    bool truthy() const {
        if (kind == NUM)  return num != 0;
        if (kind == STR)  return !str.empty();
        if (kind == MAP)  return map && !map->empty();
        if (kind == OBJ)  return true;                // 객체는 항상 참
        return list && !list->empty();
    }
    string toString(int depth = 0) const {
        if (kind == STR) return str;
        // 자기 자신을 담은 리스트/딕셔너리는 무한 재귀 → deepCopy 와 같은 한도로 차단
        if (depth > 1000)
            throw LangError("출력할 수 없습니다 (자기 자신을 포함한 구조?)");
        if (kind == LIST) {
            string out = "[";
            for (size_t i = 0; i < list->size(); i++) {
                if (i) out += ", ";
                Value& e = (*list)[i];
                out += (e.kind == STR) ? "\"" + e.str + "\"" : e.toString(depth + 1);
            }
            return out + "]";
        }
        if (kind == MAP) {
            string out = "{";
            bool first = true;
            for (auto& [k, v] : *map) {
                if (!first) out += ", ";
                first = false;
                out += "\"" + k + "\": ";
                out += (v.kind == STR) ? "\"" + v.str + "\"" : v.toString(depth + 1);
            }
            return out + "}";
        }
        if (kind == OBJ) {
            string out = className + "{";
            bool first = true;
            for (auto& [k, v] : *map) {
                if (!first) out += ", ";
                first = false;
                out += "\"" + k + "\": ";
                out += (v.kind == STR) ? "\"" + v.str + "\"" : v.toString(depth + 1);
            }
            return out + "}";
        }
        if (std::fabs(num) < 9.0e18 && num == (long long)num)
            return std::to_string((long long)num);
        std::ostringstream os; os << num; return os.str();
    }
    string kindName() const {
        return kind == NUM ? "숫자" : kind == STR ? "문자열"
             : kind == MAP ? "딕셔너리" : kind == OBJ ? "객체" : "리스트";
    }
};

// 변수 저장소 — parent 를 따라 올라가며 찾음 (지역 → 전역 스코프 체인)
struct Env {
    std::map<string, Value> vars;
    Env* parent = nullptr;
    Value* find(const string& n) {
        auto it = vars.find(n);
        if (it != vars.end()) return &it->second;
        return parent ? parent->find(n) : nullptr;
    }
    void define(const string& n, Value v) { vars[n] = std::move(v); }
    // 오타 제안 후보 — 지금 보이는 모든 변수 이름 (지역 → 전역)
    void collectNames(std::vector<string>& out) {
        for (auto& [k, v] : vars) out.push_back(k);
        if (parent) parent->collectNames(out);
    }
};

// 제어 흐름 — break/continue/return 은 exec() 의 반환값으로 올라간다.
// 예전에는 C++ 예외였는데, 예외 하나를 던지는 데 마이크로초가 들어 재귀 함수가
// CPython 보다 50배 느렸다 (fib(27): 2.0초 vs 0.04초). 반환값이면 공짜다.
// try/catch(LangError) 를 그대로 통과하는 성질도 유지된다 — 애초에 예외가 아니므로.
enum class Flow : unsigned char { NORMAL = 0, BREAK, CONTINUE, RETURN };
static Value g_retVal;  // Flow::RETURN 일 때 돌려줄 값
// ExitSignal 은 readLine 에서도 쓰므로 위쪽(플랫폼 헬퍼 앞)에 정의돼 있다.

// ============================================================
//  3. AST 노드
// ============================================================
struct Expr {
    virtual ~Expr() = default;
    virtual Value eval(Env& env) = 0;
};
using ExprP = std::unique_ptr<Expr>;

struct Stmt {
    virtual ~Stmt() = default;
    virtual Flow exec(Env& env) = 0;
};
using StmtP = std::unique_ptr<Stmt>;

struct FuncStmt;
struct ClassStmt;
static std::map<string, FuncStmt*> g_funcs;
static std::map<string, ClassStmt*> g_classes;
static Env* g_global = nullptr;
static int g_callDepth = 0;   // 재귀 깊이 추적

// ---- 표현식 ----
struct NumExpr : Expr {
    double v;
    NumExpr(double v) : v(v) {}
    Value eval(Env&) override { return Value::number(v); }
};
struct StrExpr : Expr {
    string s;
    StrExpr(string s) : s(std::move(s)) {}
    Value eval(Env&) override { return Value::text(s); }
};
struct VarExpr : Expr {
    string name; int line;
    VarExpr(string n, int l) : name(std::move(n)), line(l) {}
    Value eval(Env& env) override {
        Value* v = env.find(name);
        if (!v) {
            std::vector<string> names; env.collectNames(names);
            throw LangError(lineTag(line) + "정의되지 않은 변수: " + name + suggestName(name, names));
        }
        return *v;
    }
};
struct ListExpr : Expr {
    std::vector<ExprP> items;
    Value eval(Env& env) override {
        std::vector<Value> xs;
        for (auto& e : items) xs.push_back(e->eval(env));
        return Value::makeList(std::move(xs));
    }
};

// 딕셔너리 리터럴: {"이름": "미르", "나이": 15}
struct MapExpr : Expr {
    std::vector<std::pair<ExprP, ExprP>> items;
    int line = 0;
    Value eval(Env& env) override {
        Value m = Value::makeMap();
        for (auto& [k, v] : items) {
            Value key = k->eval(env);
            if (key.kind != Value::STR)
                throw LangError(lineTag(line) + "딕셔너리 키는 문자열이어야 합니다 (지금: " + key.kindName() + ")");
            (*m.map)[key.str] = v->eval(env);
        }
        return m;
    }
};

// 인덱스 값 검사 공통 함수 — 숫자·정수·범위 확인 후 0-기반 인덱스 반환
static size_t checkIndex(const Value& i, size_t size, int line) {
    auto err = [&](const string& m) {
        return LangError(lineTag(line) + "" + m);
    };
    if (i.kind != Value::NUM) throw err("인덱스는 숫자여야 합니다");
    if (i.num != std::floor(i.num))
        throw err("인덱스는 정수여야 합니다 (지금: " + i.toString() + ")");
    long long n = (long long)i.num;
    if (n < 1 || n > (long long)size)
        throw err("인덱스 범위 초과: " + std::to_string(n)
                  + " (리스트 크기: " + std::to_string(size) + ", 인덱스는 1부터)");
    return (size_t)(n - 1);
}

struct IndexExpr : Expr {
    ExprP target, index; int line;
    IndexExpr(ExprP t, ExprP i, int l) : target(std::move(t)), index(std::move(i)), line(l) {}
    Value eval(Env& env) override {
        Value t = target->eval(env);
        if (t.kind == Value::STR) {   // 문자열 인덱싱: s[1] → 첫 글자 (UTF-8 기준)
            auto chars = utf8Chars(t.str);
            size_t idx = checkIndex(index->eval(env), chars.size(), line);
            return Value::text(chars[idx]);
        }
        if (t.kind == Value::MAP) {   // 딕셔너리 읽기: d["키"]
            Value k = index->eval(env);
            if (k.kind != Value::STR)
                throw LangError(lineTag(line) + "딕셔너리 키는 문자열이어야 합니다 (지금: " + k.kindName() + ")");
            auto it = t.map->find(k.str);
            if (it == t.map->end())
                throw LangError(lineTag(line) + "키가 없습니다: \"" + k.str
                                + "\"  (has(딕셔너리, 키) 로 먼저 확인할 수 있어요)");
            return it->second;
        }
        if (t.kind != Value::LIST)
            throw LangError(lineTag(line) + "" + t.kindName() + "에는 [ ] 를 쓸 수 없습니다"
                            + (t.kind == Value::OBJ ? "  (객체의 필드는 obj.이름 으로 씁니다)" : ""));
        size_t idx = checkIndex(index->eval(env), t.list->size(), line);
        return (*t.list)[idx];
    }
};
// 깊은 동등 비교 — 리스트/딕셔너리/객체는 내용으로 비교. 같은 것을 가리키면 즉시 참,
// 서로 다른 순환 구조는 deepCopy/toString 과 같은 깊이 한도로 차단
static bool deepEquals(const Value& a, const Value& b, int depth, int line) {
    if (depth > 1000)
        throw LangError(lineTag(line) + "비교할 수 없습니다 (자기 자신을 포함한 구조?)");
    if (a.kind != b.kind) return false;
    if (a.kind == Value::NUM) return a.num == b.num;
    if (a.kind == Value::STR) return a.str == b.str;
    if (a.kind == Value::LIST) {
        if (a.list == b.list) return true;
        if (a.list->size() != b.list->size()) return false;
        for (size_t i = 0; i < a.list->size(); i++)
            if (!deepEquals((*a.list)[i], (*b.list)[i], depth + 1, line)) return false;
        return true;
    }
    // MAP / OBJ
    if (a.map == b.map) return true;
    if (a.className != b.className) return false;
    if (a.map->size() != b.map->size()) return false;
    auto ia = a.map->begin(), ib = b.map->begin();
    for (; ia != a.map->end(); ++ia, ++ib) {
        if (ia->first != ib->first) return false;
        if (!deepEquals(ia->second, ib->second, depth + 1, line)) return false;
    }
    return true;
}
// 나머지 — 결과가 나누는 수의 부호를 따른다 (수학·파이썬 관례).
// C 의 fmod 는 나눠지는 수의 부호를 따라 -7 % 3 이 -1 이 되는데, 그러면 시저 암호처럼
// 음수를 되감는 교과서 예제가 파이썬과 다른 답을 낸다.
static double floorMod(double a, double b) {
    double r = std::fmod(a, b);
    if (r != 0 && ((r < 0) != (b < 0))) r += b;
    return r;
}
// 문자열 반복 횟수 검사 — "*" * 5 의 5 자리. 음수는 파이썬처럼 빈 문자열이 된다.
static const size_t REPEAT_CAP = 10000000;   // 폭주한 반복이 브라우저를 먹지 않게
static size_t repeatCount(double n, size_t unit, const std::function<LangError(const string&)>& err) {
    if (n != std::floor(n)) throw err("문자열을 소수 번 반복할 수는 없습니다 (지금: " + Value::number(n).toString() + ")");
    if (n <= 0) return 0;
    if (n > (double)REPEAT_CAP || unit * (size_t)n > REPEAT_CAP)
        throw err("문자열 반복이 너무 깁니다 (최대 " + std::to_string(REPEAT_CAP) + "자)");
    return (size_t)n;
}
// 이항 연산의 실제 처리 — BinExpr 와 원소 복합 대입(xs[i] += ...)이 공유
static Value applyBin(Tok op, const Value& a, const Value& b, int line) {
    auto err = [&](const string& m) {
        return LangError(lineTag(line) + "" + m);
    };
    if (op == Tok::PLUS && (a.kind == Value::STR || b.kind == Value::STR))
        return Value::text(a.toString() + b.toString());
    // 문자열 * 숫자 = 그만큼 반복 — 별 찍기, 막대그래프, 구분선
    if (op == Tok::STAR && ((a.kind == Value::STR) != (b.kind == Value::STR))) {
        const Value& s = (a.kind == Value::STR) ? a : b;
        const Value& n = (a.kind == Value::STR) ? b : a;
        if (n.kind != Value::NUM)
            throw err("문자열은 숫자만큼만 반복할 수 있습니다 (지금: " + n.kindName() + ")");
        size_t k = repeatCount(n.num, s.str.size(), err);
        string out;
        out.reserve(s.str.size() * k);
        for (size_t i = 0; i < k; i++) out += s.str;
        return Value::text(out);
    }
    // 리스트 + 리스트 = 이어붙인 새 리스트
    if (op == Tok::PLUS && a.kind == Value::LIST && b.kind == Value::LIST) {
        std::vector<Value> xs = *a.list;
        xs.insert(xs.end(), b.list->begin(), b.list->end());
        return Value::makeList(std::move(xs));
    }
    // ==/!= 는 모든 타입 허용 — 타입이 다르면 false/true, 리스트/딕셔너리/객체는 깊은 비교
    if (op == Tok::EQ || op == Tok::NEQ)
        return Value::number((op == Tok::EQ) == deepEquals(a, b, 0, line) ? 1 : 0);
    auto cmp = [&](auto f) {
        if (a.kind == Value::LIST || b.kind == Value::LIST
         || a.kind == Value::MAP  || b.kind == Value::MAP
         || a.kind == Value::OBJ  || b.kind == Value::OBJ)
            throw err("리스트/딕셔너리/객체는 비교 연산을 지원하지 않습니다");
        if (a.kind != b.kind)
            throw err("숫자와 문자열은 비교할 수 없습니다"
                      "  (숫자처럼 보이는 문자열이면 num() 으로 바꿔 쓰세요)");
        bool r = (a.kind == Value::NUM) ? f(a.num, b.num) : f(a.str, b.str);
        return Value::number(r ? 1 : 0);
    };
    switch (op) {
        case Tok::LT:  return cmp([](auto x, auto y) { return x <  y; });
        case Tok::GT:  return cmp([](auto x, auto y) { return x >  y; });
        case Tok::LE:  return cmp([](auto x, auto y) { return x <= y; });
        case Tok::GE:  return cmp([](auto x, auto y) { return x >= y; });
        default: break;
    }
    if (a.kind != Value::NUM || b.kind != Value::NUM)
        throw err(a.kindName() + josa(a.kindName(), "과", "와") + " " + b.kindName()
                  + josa(b.kindName(), "은", "는") + " 이 연산이 안 됩니다");
    switch (op) {
        case Tok::PLUS:  return Value::number(a.num + b.num);
        case Tok::MINUS: return Value::number(a.num - b.num);
        case Tok::STAR:  return Value::number(a.num * b.num);
        case Tok::SLASH:
            if (b.num == 0) throw err("0으로 나눌 수 없습니다");
            return Value::number(a.num / b.num);
        case Tok::PERCENT:
            if (b.num == 0) throw err("0으로 나머지 연산을 할 수 없습니다");
            return Value::number(floorMod(a.num, b.num));
        default: throw err("지원하지 않는 연산자");
    }
}
// 필드 읽기: obj.이름
struct FieldExpr : Expr {
    ExprP target; string field; int line;
    Value eval(Env& env) override {
        Value t = target->eval(env);
        if (t.kind != Value::OBJ)
            throw LangError(lineTag(line) + "" + t.kindName()
                            + "에는 . 필드를 쓸 수 없습니다 (딕셔너리는 [\"키\"] 를 쓰세요)");
        auto it = t.map->find(field);
        if (it == t.map->end()) {
            std::vector<string> names;
            for (auto& [k, v] : *t.map) names.push_back(k);
            throw LangError(lineTag(line) + "필드가 없습니다: ." + field + suggestName(field, names));
        }
        return it->second;
    }
};
struct MethodCallExpr : Expr {   // obj.메서드(인자들) — 정의는 ClassStmt 뒤에
    ExprP target; string method; std::vector<ExprP> args; int line;
    Value eval(Env& env) override;
};
struct BinExpr : Expr {
    Tok op; ExprP lhs, rhs; int line;
    // 문자열 보간 "이름: {x}" 은 파싱 시점에 ("" + "이름: ") + x 로 풀린다.
    // 그러면 f-string 으로 되돌릴 정보가 사라지므로, 체인의 루트에만 조각 수를 남긴다.
    // 인터프리터와 C++ 백엔드는 이 값을 보지 않는다 — PyGen 만 쓴다.
    int interpN = 0;
    BinExpr(Tok op, ExprP l, ExprP r, int ln)
        : op(op), lhs(std::move(l)), rhs(std::move(r)), line(ln) {}
    Value eval(Env& env) override {
        return applyBin(op, lhs->eval(env), rhs->eval(env), line);
    }
};
struct NegExpr : Expr {
    ExprP inner; int line;
    NegExpr(ExprP e, int l) : inner(std::move(e)), line(l) {}
    Value eval(Env& env) override {
        Value v = inner->eval(env);
        if (v.kind != Value::NUM)
            throw LangError(lineTag(line) + v.kindName() + "에는 - 를 붙일 수 없습니다");
        return Value::number(-v.num);
    }
};
struct NotExpr : Expr {
    ExprP inner;
    NotExpr(ExprP e) : inner(std::move(e)) {}
    Value eval(Env& env) override {
        return Value::number(inner->eval(env).truthy() ? 0 : 1);
    }
};
struct InputExpr : Expr {
    string prompt;
    InputExpr(string p) : prompt(std::move(p)) {}
    Value eval(Env&) override {
#ifdef VENOS_WASM
        g_pendingPrompt = prompt;
#endif
        if (!prompt.empty()) std::cout << prompt << std::flush;
        string line;
        if (!readLine(line))
            throw LangError("입력을 읽을 수 없습니다");
        line = trim(line);   // 앞뒤 공백 제거 (공백 때문에 숫자 인식 실패 방지)
        // num() 과 같은 문법만 숫자로 본다 — `0x10` 이나 `inf` 를 숫자로 받아 버리면
        // 같은 입력에 파이썬 버전이 다른 답을 낸다 (파이썬은 그걸 문자열로 둔다)
        double d;
        if (strToNum(line, d)) return Value::number(d);
        return Value::text(line);
    }
};
struct LogicalExpr : Expr {
    Tok op; ExprP lhs, rhs;
    LogicalExpr(Tok op, ExprP l, ExprP r)
        : op(op), lhs(std::move(l)), rhs(std::move(r)) {}
    Value eval(Env& env) override {
        bool left = lhs->eval(env).truthy();
        if (op == Tok::AND) {
            if (!left) return Value::number(0);
            return Value::number(rhs->eval(env).truthy() ? 1 : 0);
        } else {
            if (left) return Value::number(1);
            return Value::number(rhs->eval(env).truthy() ? 1 : 0);
        }
    }
};

// ---- 문장 ----
struct LetStmt : Stmt {
    string name; ExprP val;
    LetStmt(string n, ExprP v) : name(std::move(n)), val(std::move(v)) {}
    Flow exec(Env& env) override { env.define(name, val->eval(env)); return Flow::NORMAL; }
};
struct AssignStmt : Stmt {
    string name; ExprP val; int line;
    AssignStmt(string n, ExprP v, int l) : name(std::move(n)), val(std::move(v)), line(l) {}
    Flow exec(Env& env) override {
        Value* slot = env.find(name);
        if (!slot) {
            std::vector<string> names; env.collectNames(names);
            string hint = suggestName(name, names);
            if (hint.empty()) hint = "  (" + KW_LET + " " + name + " = ... 로 먼저 선언하세요)";
            throw LangError(lineTag(line) + "선언되지 않은 변수에 대입: " + name + hint);
        }
        *slot = val->eval(env);
        return Flow::NORMAL;
    }
};
// 경로 접근자: xs[i] 같은 인덱스이거나 obj.필드
struct Accessor {
    bool isField = false;
    string field;      // isField 일 때
    ExprP index;       // 인덱스일 때
    int line = 0;
};

// 체인 중간 단계 접근 (중간은 반드시 존재해야 함)
static Value* stepIntoAcc(Value* cur, Accessor& a, Env& env) {
    auto err = [&](const string& m) {
        return LangError(lineTag(a.line) + "" + m);
    };
    if (a.isField) {
        if (cur->kind != Value::OBJ)
            throw err(cur->kindName() + "에는 . 필드를 쓸 수 없습니다 (딕셔너리는 [\"키\"] 를 쓰세요)");
        auto it = cur->map->find(a.field);
        if (it == cur->map->end()) {
            std::vector<string> names;
            for (auto& [k, v] : *cur->map) names.push_back(k);
            throw err("필드가 없습니다: ." + a.field + suggestName(a.field, names));
        }
        return &it->second;
    }
    Value key = a.index->eval(env);
    if (cur->kind == Value::LIST) {
        size_t idx = checkIndex(key, cur->list->size(), a.line);
        return &(*cur->list)[idx];
    }
    if (cur->kind == Value::MAP) {
        if (key.kind != Value::STR)
            throw err("딕셔너리 키는 문자열이어야 합니다 (지금: " + key.kindName() + ")");
        auto it = cur->map->find(key.str);
        if (it == cur->map->end()) throw err("키가 없습니다: \"" + key.str + "\"");
        return &it->second;
    }
    if (cur->kind == Value::STR)
        throw err("문자열의 글자는 직접 바꿀 수 없습니다 (replace() 를 쓰세요)");
    throw err(cur->kindName() + "에는 [ ] 를 쓸 수 없습니다"
              + (cur->kind == Value::OBJ ? "  (객체의 필드는 obj.이름 으로 씁니다)" : ""));
}

// 마지막 단계 대입용 슬롯 (딕셔너리 키/객체 필드는 새로 생성 가능)
static Value* putSlot(Value* cur, Accessor& a, Env& env) {
    auto err = [&](const string& m) {
        return LangError(lineTag(a.line) + "" + m);
    };
    if (a.isField) {
        if (cur->kind != Value::OBJ)
            throw err(cur->kindName() + "에는 . 필드를 쓸 수 없습니다");
        return &(*cur->map)[a.field];               // 없으면 생성
    }
    Value key = a.index->eval(env);
    if (cur->kind == Value::LIST) {
        size_t idx = checkIndex(key, cur->list->size(), a.line);
        return &(*cur->list)[idx];
    }
    if (cur->kind == Value::MAP) {
        if (key.kind != Value::STR)
            throw err("딕셔너리 키는 문자열이어야 합니다 (지금: " + key.kindName() + ")");
        return &(*cur->map)[key.str];               // 없으면 생성
    }
    if (cur->kind == Value::STR)
        throw err("문자열의 글자는 직접 바꿀 수 없습니다 (replace() 를 쓰세요)");
    throw err(cur->kindName() + "에는 [ ] 를 쓸 수 없습니다"
              + (cur->kind == Value::OBJ ? "  (객체의 필드는 obj.이름 으로 씁니다)" : ""));
}

// 경로 대입: x[1] = v,  obj.필드 = v,  obj.점수[2] = v ...
struct PathAssignStmt : Stmt {
    string name; std::vector<Accessor> path; ExprP val; int line = 0;
    Flow exec(Env& env) override {
        Value* cur = env.find(name);
        if (!cur) {
            std::vector<string> names; env.collectNames(names);
            throw LangError(lineTag(line) + "정의되지 않은 변수: " + name + suggestName(name, names));
        }
        for (size_t k = 0; k + 1 < path.size(); k++)
            cur = stepIntoAcc(cur, path[k], env);
        cur = putSlot(cur, path.back(), env);
        *cur = val->eval(env);
        return Flow::NORMAL;
    }
};
// 경로 복합 대입: xs[i] += 1,  obj.나이 += 1  (기존 값이 있어야 함)
struct PathCompoundStmt : Stmt {
    string name; std::vector<Accessor> path; Tok op; ExprP rhs; int line = 0;
    Flow exec(Env& env) override {
        Value* cur = env.find(name);
        if (!cur) {
            std::vector<string> names; env.collectNames(names);
            throw LangError(lineTag(line) + "정의되지 않은 변수: " + name + suggestName(name, names));
        }
        for (auto& a : path)
            cur = stepIntoAcc(cur, a, env);
        *cur = applyBin(op, *cur, rhs->eval(env), line);
        return Flow::NORMAL;
    }
};
struct PrintStmt : Stmt {
    std::vector<ExprP> vals;   // print a, b, c → 공백으로 이어서 출력
    Flow exec(Env& env) override {
        string out;
        for (size_t i = 0; i < vals.size(); i++) {
            if (i) out += " ";
            out += vals[i]->eval(env).toString();
        }
        std::cout << out << "\n";
        return Flow::NORMAL;
    }
};
struct ExprStmt : Stmt {
    ExprP e;
    ExprStmt(ExprP e) : e(std::move(e)) {}
    Flow exec(Env& env) override { e->eval(env); return Flow::NORMAL; }
};
struct BlockStmt : Stmt {
    std::vector<StmtP> stmts;
    Flow exec(Env& env) override {
        for (auto& s : stmts) {
            Flow f = s->exec(env);
            if (f != Flow::NORMAL) return f;     // break/continue/return 은 위로
        }
        return Flow::NORMAL;
    }
};
struct IfStmt : Stmt {
    ExprP cond; StmtP thenB, elseB;   // elseB 는 블록이거나 또 다른 IfStmt (else if 체인)
    Flow exec(Env& env) override {
        if (cond->eval(env).truthy()) return thenB->exec(env);
        if (elseB) return elseB->exec(env);
        return Flow::NORMAL;
    }
};
// try { ... } catch 오류 { ... } — 런타임 에러를 잡아 메시지를 변수에 담음.
// break/continue/return/exit 는 에러가 아니므로 그대로 통과한다.
struct TryStmt : Stmt {
    StmtP tryB, catchB;
    string var;
    Flow exec(Env& env) override {
        try {
            return tryB->exec(env);                  // break/continue/return 은 그대로 통과
        } catch (LangError& e) {
            env.vars[var] = Value::text(e.what());   // 현재 스코프의 지역 변수로
            return catchB->exec(env);
        }
    }
};

struct WhileStmt : Stmt {
    ExprP cond; StmtP body;
    Flow exec(Env& env) override {
#ifdef VENOS_WASM
        // 웹에선 무한 루프가 탭을 얼리므로 상한 유지. 네이티브는 상한 없음 (빌드본과 동작 일치)
        long long guard = 0;
#endif
        while (cond->eval(env).truthy()) {
            pumpWeb();
            Flow f = body->exec(env);
            if (f == Flow::BREAK)  break;
            if (f == Flow::RETURN) return f;
#ifdef VENOS_WASM
            if (++guard > 10'000'000)
                throw LangError("반복 횟수가 너무 많습니다 (무한 루프?)");
#endif
        }
        return Flow::NORMAL;
    }
};
// for i = 1 to 10 (step 2) { ... }  — 양끝 포함, step 생략 시 방향 자동
struct ForStmt : Stmt {
    string var; ExprP start, end, step; StmtP body; int line;
    Flow exec(Env& env) override {
        auto err = [&](const string& m) {
            return LangError(lineTag(line) + "" + m);
        };
        Value s = start->eval(env), e = end->eval(env);
        if (s.kind != Value::NUM || e.kind != Value::NUM)
            throw err(KW_FOR + " 의 시작/끝 값은 숫자여야 합니다");
        double stepv;
        if (step) {
            Value sv = step->eval(env);
            if (sv.kind != Value::NUM || sv.num == 0)
                throw err(KW_STEP + " 은 0이 아닌 숫자여야 합니다");
            stepv = sv.num;
        } else {
            stepv = (s.num <= e.num) ? 1 : -1;
        }
        // 루프 변수는 항상 "현재 스코프"의 지역 변수
        // (find 로 부모 체인을 타면 재귀 호출끼리 전역 변수를 공유하는 버그가 생김)
        env.vars[var] = Value::number(0);
        Value* slot = &env.vars[var];
        for (double i = s.num; stepv > 0 ? i <= e.num : i >= e.num; i += stepv) {
            pumpWeb();
            *slot = Value::number(i);
            Flow f = body->exec(env);
            if (f == Flow::BREAK)  break;
            if (f == Flow::RETURN) return f;
        }
        return Flow::NORMAL;
    }
};
// for x in xs { ... } — 리스트/문자열 순회 (스냅샷 방식: 순회 중 수정해도 안전)
struct ForEachStmt : Stmt {
    string var; ExprP iter; StmtP body; int line;
    Flow exec(Env& env) override {
        Value it = iter->eval(env);
        std::vector<Value> items;
        if (it.kind == Value::LIST) items = *it.list;
        else if (it.kind == Value::STR) {
            for (auto& ch : utf8Chars(it.str)) items.push_back(Value::text(ch));
        } else if (it.kind == Value::MAP) {   // 딕셔너리는 키를 순회 (정렬 순서)
            for (auto& [k, v] : *it.map) items.push_back(Value::text(k));
        } else {
            throw LangError(lineTag(line) + "" + KW_FOR + " ... " + KW_IN
                            + " 은 리스트/문자열/딕셔너리만 순회할 수 있습니다 (지금: " + it.kindName() + ")");
        }
        env.vars[var] = Value::number(0);
        Value* slot = &env.vars[var];
        for (auto& e : items) {
            pumpWeb();
            *slot = e;
            Flow f = body->exec(env);
            if (f == Flow::BREAK)  break;
            if (f == Flow::RETURN) return f;
        }
        return Flow::NORMAL;
    }
};

struct BreakStmt : Stmt {
    Flow exec(Env&) override { return Flow::BREAK; }
};
struct ContinueStmt : Stmt {
    Flow exec(Env&) override { return Flow::CONTINUE; }
};
struct ReturnStmt : Stmt {
    ExprP val;
    Flow exec(Env& env) override {
        g_retVal = val ? val->eval(env) : Value::number(0);
        return Flow::RETURN;
    }
};
struct FuncStmt : Stmt {
    string name;
    std::vector<string> params;
    StmtP body;
    Flow exec(Env&) override { g_funcs[name] = this; return Flow::NORMAL; }
};

// class 이름 { func ... }  — 메서드 묶음. init 이 생성자.
struct ClassStmt : Stmt {
    string name;
    std::vector<std::unique_ptr<FuncStmt>> methodList;   // 소유권
    std::map<string, FuncStmt*> methods;                  // 이름 → 메서드
    Flow exec(Env&) override { g_classes[name] = this; return Flow::NORMAL; }
};

// 메서드 실행 공통부: self + 인자를 지역 스코프에 바인딩하고 본문 실행
static Value runMethod(ClassStmt* cls, FuncStmt* fn, Value& self,
                       std::vector<Value>& args, int line);

// 깊은 복사 — 리스트/딕셔너리/객체를 재귀적으로 새로 만든다
static Value deepCopy(const Value& v, int depth, int line) {
    if (depth > 1000)
        throw LangError(lineTag(line) + "복사할 수 없습니다 (자기 자신을 포함한 구조?)");
    if (v.kind == Value::LIST) {
        std::vector<Value> xs;
        for (auto& e : *v.list) xs.push_back(deepCopy(e, depth + 1, line));
        return Value::makeList(std::move(xs));
    }
    if (v.kind == Value::MAP || v.kind == Value::OBJ) {
        Value out;
        out.kind = v.kind;
        out.className = v.className;
        out.map = std::make_shared<std::map<string, Value>>();
        for (auto& [k, e] : *v.map) (*out.map)[k] = deepCopy(e, depth + 1, line);
        return out;
    }
    return v;   // 숫자/문자열은 원래 값 복사
}

// 재귀 깊이 카운터 — 생성 시 +1, 소멸 시 -1 (예외로 빠져나가도 자동 복원)
struct DepthGuard {
    DepthGuard(int line, const string& name) {
        pumpWeb();   // 루프 없이 재귀만 도는 프로그램(fib)도 멈출 수 있게. 프레임을
                     // 건드리기 전에 부른다 — 여기서 ExitSignal 이 나가도 셈이 어긋나지 않는다
        if (++g_callDepth > MAX_RECURSION) {
            --g_callDepth;
            throw LangError(lineTag(line) + "함수 호출이 너무 깊습니다 (재귀 "
                            + std::to_string(MAX_RECURSION) + "회 초과 — 무한 재귀?)"
#ifdef VENOS_WASM
                            + "  (브라우저에서는 " + std::to_string(MAX_RECURSION)
                            + "까지만 됩니다. 내려받아 쓰면 " + std::to_string(RECURSION_DESKTOP) + ")"
#endif
                            );
        }
        g_frames.push_back({ &name, line });
    }
    ~DepthGuard() { --g_callDepth; g_frames.pop_back(); }
};

Value MethodCallExpr::eval(Env& env) {
    Value obj = target->eval(env);
    auto err = [&](const string& m) {
        return LangError(lineTag(line) + "" + m);
    };
    if (obj.kind != Value::OBJ) {
        // "abc".upper() 는 파이썬 버릇이다. 그 이름이 내장 함수면 쓰는 법을 알려 준다.
        throw err(obj.kindName() + "에는 메서드를 호출할 수 없습니다" + methodHint(method));
    }
    auto cit = g_classes.find(obj.className);
    if (cit == g_classes.end()) throw err("알 수 없는 클래스: " + obj.className);
    auto mit = cit->second->methods.find(method);
    if (mit == cit->second->methods.end()) {
        std::vector<string> names;
        for (auto& [k, v] : cit->second->methods) names.push_back(k);
        throw err("클래스 '" + obj.className + "' 에 메서드 '" + method + "'"
                  + josa(method, "이", "가") + " 없습니다" + suggestName(method, names));
    }
    FuncStmt* fn = mit->second;
    if (args.size() != fn->params.size())
        throw err(method + "() 는 인자 " + std::to_string(fn->params.size())
                  + "개가 필요합니다 (지금 " + std::to_string(args.size()) + "개)");
    std::vector<Value> vals;
    vals.reserve(args.size());
    for (auto& a : args) vals.push_back(a->eval(env));
    return runMethod(cit->second, fn, obj, vals, line);
}

struct CallExpr : Expr {
    string name; std::vector<ExprP> args; int line;
    CallExpr(string n, std::vector<ExprP> a, int l)
        : name(std::move(n)), args(std::move(a)), line(l) {}
    Value eval(Env& env) override {
        auto err = [&](const string& m) {
            return LangError(lineTag(line) + "" + m);
        };
        std::vector<Value> vals;
        vals.reserve(args.size());
        for (auto& a : args) vals.push_back(a->eval(env));
        auto needNum = [&](size_t i) {
            if (vals[i].kind != Value::NUM)
                throw err(name + "() 의 " + std::to_string(i + 1) + "번째 인자는 숫자여야 합니다");
            return vals[i].num;
        };
        auto needArgs = [&](size_t n, const string& usage) {
            if (vals.size() != n) throw err(usage + " 는 인자 " + std::to_string(n) + "개가 필요합니다");
        };

        // ---- 내장 함수 ----
        if (name == "random") {   // random(a, b): a 이상 b 이하 정수 무작위
            needArgs(2, "random(최소, 최대)");
            long long a = (long long)needNum(0), b = (long long)needNum(1);
            if (a > b) std::swap(a, b);
            static std::mt19937_64 rng{ std::random_device{}() };
            std::uniform_int_distribution<long long> dist(a, b);
            return Value::number((double)dist(rng));
        }
        if (name == "round") {     // round(3.7) → 4 / round(3.14159, 2) → 3.14
            if (vals.size() != 1 && vals.size() != 2)
                throw err("round(숫자) 또는 round(숫자, 자릿수) 로 써야 합니다");
            double x = needNum(0);
            if (vals.size() == 1) return Value::number(std::round(x));
            double d = needNum(1);
            if (d != std::floor(d) || d < 0 || d > 15)
                throw err("round() 의 자릿수는 0 이상 15 이하의 정수여야 합니다");
            double p = std::pow(10.0, d);
            return Value::number(std::round(x * p) / p);
        }
        if (name == "floor") { needArgs(1, "floor(숫자)"); return Value::number(std::floor(needNum(0))); }
        if (name == "ceil")  { needArgs(1, "ceil(숫자)");  return Value::number(std::ceil(needNum(0)));  }
        if (name == "abs")   { needArgs(1, "abs(숫자)");   return Value::number(std::fabs(needNum(0)));  }
        if (name == "sqrt")  {
            needArgs(1, "sqrt(숫자)");
            double x = needNum(0);
            if (x < 0) throw err("sqrt() 에 음수는 넣을 수 없습니다");
            return Value::number(std::sqrt(x));
        }
        if (name == "min") { needArgs(2, "min(a, b)"); return Value::number(std::min(needNum(0), needNum(1))); }
        if (name == "max") { needArgs(2, "max(a, b)"); return Value::number(std::max(needNum(0), needNum(1))); }
        if (name == "num") {
            needArgs(1, "num(값)");
            if (vals[0].kind == Value::NUM) return vals[0];
            if (vals[0].kind == Value::STR) {
                double d;
                if (strToNum(vals[0].str, d)) return Value::number(d);
                throw err("숫자로 바꿀 수 없는 문자열: \"" + ellipsize(vals[0].str, 40) + "\"");
            }
            throw err("리스트는 숫자로 바꿀 수 없습니다");
        }
        if (name == "str") { needArgs(1, "str(값)"); return Value::text(vals[0].toString()); }
        if (name == "len") {
            needArgs(1, "len(값)");
            if (vals[0].kind == Value::LIST) return Value::number((double)vals[0].list->size());
            if (vals[0].kind == Value::STR)  return Value::number((double)utf8Length(vals[0].str));
            if (vals[0].kind == Value::MAP)  return Value::number((double)vals[0].map->size());
            throw err("len() 은 리스트/문자열/딕셔너리에만 쓸 수 있습니다");
        }
        if (name == "push") {
            needArgs(2, "push(리스트, 값)");
            if (vals[0].kind != Value::LIST) throw err("push() 의 1번째 인자는 리스트여야 합니다");
            vals[0].list->push_back(vals[1]);
            return vals[0];
        }
        if (name == "pop") {      // pop(리스트): 마지막 원소를 빼서 돌려줌
            needArgs(1, "pop(리스트)");
            if (vals[0].kind != Value::LIST) throw err("pop() 의 인자는 리스트여야 합니다");
            if (vals[0].list->empty()) throw err("빈 리스트에서는 pop() 할 수 없습니다");
            Value back = vals[0].list->back();
            vals[0].list->pop_back();
            return back;
        }
        if (name == "sort") {     // sort(리스트): 오름차순 정렬 (숫자끼리 or 문자열끼리)
            needArgs(1, "sort(리스트)");
            if (vals[0].kind != Value::LIST) throw err("sort() 의 인자는 리스트여야 합니다");
            auto& xs = *vals[0].list;
            bool allNum = true, allStr = true;
            for (auto& x : xs) {
                if (x.kind != Value::NUM) allNum = false;
                if (x.kind != Value::STR) allStr = false;
            }
            if (!allNum && !allStr)
                throw err("sort() 는 숫자만 있거나 문자열만 있는 리스트만 정렬할 수 있습니다");
            if (allNum) std::sort(xs.begin(), xs.end(), [](const Value& a, const Value& b) { return a.num < b.num; });
            else        std::sort(xs.begin(), xs.end(), [](const Value& a, const Value& b) { return a.str < b.str; });
            return vals[0];
        }

        auto needStr = [&](size_t i) -> const string& {
            if (vals[i].kind != Value::STR)
                throw err(name + "() 의 " + std::to_string(i + 1) + "번째 인자는 문자열이어야 합니다");
            return vals[i].str;
        };
        if (name == "split") {     // split("a,b,c", ",") → ["a","b","c"]
            needArgs(2, "split(문자열, 구분자)");
            const string& s = needStr(0);
            const string& sep = needStr(1);
            if (sep.empty()) throw err("split() 의 구분자는 빈 문자열일 수 없습니다");
            std::vector<Value> parts;
            size_t start = 0, p;
            while ((p = s.find(sep, start)) != string::npos) {
                parts.push_back(Value::text(s.substr(start, p - start)));
                start = p + sep.size();
            }
            parts.push_back(Value::text(s.substr(start)));
            return Value::makeList(std::move(parts));
        }
        if (name == "join") {      // join(["a","b"], "-") → "a-b"
            needArgs(2, "join(리스트, 구분자)");
            if (vals[0].kind != Value::LIST) throw err("join() 의 1번째 인자는 리스트여야 합니다");
            const string& sep = needStr(1);
            string out;
            for (size_t i = 0; i < vals[0].list->size(); i++) {
                if (i) out += sep;
                out += (*vals[0].list)[i].toString();
            }
            return Value::text(out);
        }
        if (name == "upper" || name == "lower") {   // 영문만 변환 (한글은 그대로)
            needArgs(1, name + "(문자열)");
            string s = needStr(0);
            for (auto& c : s)
                c = (name == "upper") ? toupper((unsigned char)c) : tolower((unsigned char)c);
            return Value::text(s);
        }
        if (name == "find") {      // find("안녕하세요", "하세") → 3 (글자 위치, 없으면 0)
            needArgs(2, "find(문자열, 찾을것) 또는 find(리스트, 값)");
            if (vals[0].kind == Value::LIST) {    // 리스트에서 값의 위치 (순차 탐색)
                auto& xs = *vals[0].list;
                for (size_t i = 0; i < xs.size(); i++)
                    if (deepEquals(xs[i], vals[1], 0, line)) return Value::number((double)(i + 1));
                return Value::number(0);
            }
            auto hay = utf8Chars(needStr(0));
            auto nee = utf8Chars(needStr(1));
            if (nee.empty()) throw err("find() 로 빈 문자열은 찾을 수 없습니다");
            if (nee.size() <= hay.size()) {
                for (size_t i = 0; i + nee.size() <= hay.size(); i++) {
                    bool ok = true;
                    for (size_t j = 0; j < nee.size(); j++)
                        if (hay[i + j] != nee[j]) { ok = false; break; }
                    if (ok) return Value::number((double)(i + 1));
                }
            }
            return Value::number(0);
        }
        if (name == "replace") {   // replace("aXbXc", "X", "-") → "a-b-c" (전부 교체)
            needArgs(3, "replace(문자열, 바꿀것, 새것)");
            string s = needStr(0);
            const string& from = needStr(1);
            const string& to = needStr(2);
            if (from.empty()) throw err("replace() 의 바꿀 문자열은 비어 있을 수 없습니다");
            string out;
            size_t start = 0, p;
            while ((p = s.find(from, start)) != string::npos) {
                out += s.substr(start, p - start);
                out += to;
                start = p + from.size();
            }
            out += s.substr(start);
            return Value::text(out);
        }
        if (name == "substr") {    // substr("안녕하세요", 2, 3) → "녕하세" (글자 기준, 1부터)
            needArgs(3, "substr(문자열, 시작, 개수)");
            auto chars = utf8Chars(needStr(0));
            double st = needNum(1), cn = needNum(2);
            if (st != std::floor(st) || cn != std::floor(cn))
                throw err("substr() 의 시작/개수는 정수여야 합니다");
            long long start = (long long)st, count = (long long)cn;
            if (start < 1) throw err("substr() 의 시작 위치는 1 이상이어야 합니다");
            if (count < 0) throw err("substr() 의 개수는 0 이상이어야 합니다");
            string out;
            for (long long i = start - 1; i < (long long)chars.size() && i < start - 1 + count; i++)
                out += chars[i];
            return Value::text(out);
        }
        if (name == "readfile") {  // readfile("data.txt") → 파일 전체를 문자열로
            needArgs(1, "readfile(경로)");
            std::FILE* fp = openFile(needStr(0), "rb", L"rb");
            if (!fp) throw err("파일을 열 수 없습니다: " + vals[0].str);
            return Value::text(readAll(fp));
        }
        if (name == "writefile") { // writefile("out.txt", 내용) → 파일에 저장
            needArgs(2, "writefile(경로, 내용)");
            std::FILE* fp = openFile(needStr(0), "wb", L"wb");
            if (!fp) throw err("파일을 만들 수 없습니다: " + vals[0].str);
            writeAll(fp, vals[1].toString());
            return Value::number(1);
        }
        if (name == "time") {      // time() → 1970년부터 지난 초 (소수점 포함)
            needArgs(0, "time()");
            auto now = std::chrono::system_clock::now().time_since_epoch();
            return Value::number(std::chrono::duration<double>(now).count());
        }
        if (name == "exists") {    // exists("save.txt") → 파일 있으면 true
            needArgs(1, "exists(경로)");
            // "열리는가"가 아니라 "있는가"를 묻는다 — 트랜스파일 빌드본과 같은 판단이어야 한다
            std::error_code ec;
            return Value::number(fs::is_regular_file(toPath(needStr(0)), ec) ? 1 : 0);
        }
        if (name == "appendfile") { // appendfile(경로, 내용) → 파일 끝에 이어쓰기
            needArgs(2, "appendfile(경로, 내용)");
            std::FILE* fp = openFile(needStr(0), "ab", L"ab");
            if (!fp) throw err("파일을 열 수 없습니다: " + vals[0].str);
            writeAll(fp, vals[1].toString());
            return Value::number(1);
        }
        if (name == "error") {     // error("메시지") → 일부러 에러 발생 (try 로 잡기)
            needArgs(1, "error(메시지)");
            throw LangError(vals[0].toString());
        }
        if (name == "copy") {      // copy(값) → 깊은 복사본 (원본과 독립)
            needArgs(1, "copy(값)");
            return deepCopy(vals[0], 0, line);
        }
        if (name == "exit") {      // exit() → 프로그램 즉시 종료
            needArgs(0, "exit()");
            throw ExitSignal{};
        }
        if (name == "keys") {      // keys(d) → 키들의 리스트 (정렬 순서)
            needArgs(1, "keys(딕셔너리)");
            if (vals[0].kind != Value::MAP) throw err("keys() 의 인자는 딕셔너리여야 합니다");
            std::vector<Value> out;
            for (auto& [k, v] : *vals[0].map) out.push_back(Value::text(k));
            return Value::makeList(std::move(out));
        }
        if (name == "has") {       // has(d, "키") / has(리스트, 값) → true/false
            needArgs(2, "has(딕셔너리, 키) 또는 has(리스트, 값)");
            if (vals[0].kind == Value::LIST) {
                for (auto& x : *vals[0].list)
                    if (deepEquals(x, vals[1], 0, line)) return Value::number(1);
                return Value::number(0);
            }
            if (vals[0].kind != Value::MAP)
                throw err("has() 의 1번째 인자는 딕셔너리나 리스트여야 합니다");
            return Value::number(vals[0].map->count(needStr(1)) ? 1 : 0);
        }
        if (name == "reverse") {   // reverse(리스트) → 제자리 뒤집기 / reverse("문자열") → 뒤집은 새 문자열
            needArgs(1, "reverse(리스트) 또는 reverse(문자열)");
            if (vals[0].kind == Value::LIST) {
                std::reverse(vals[0].list->begin(), vals[0].list->end());
                return vals[0];
            }
            if (vals[0].kind == Value::STR) {
                auto cs = utf8Chars(vals[0].str);
                string out;
                for (size_t i = cs.size(); i > 0; i--) out += cs[i - 1];
                return Value::text(out);
            }
            throw err("reverse() 의 인자는 리스트나 문자열이어야 합니다");
        }
        if (name == "remove") {    // remove(d, "키") → 있었으면 1 / remove(xs, i) → 빠진 원소
            needArgs(2, "remove(딕셔너리, 키) 또는 remove(리스트, 위치)");
            if (vals[0].kind == Value::MAP)
                return Value::number(vals[0].map->erase(needStr(1)) ? 1 : 0);
            if (vals[0].kind == Value::LIST) {
                size_t i = checkIndex(vals[1], vals[0].list->size(), line);
                Value removed = (*vals[0].list)[i];
                vals[0].list->erase(vals[0].list->begin() + i);
                return removed;
            }
            throw err("remove() 는 딕셔너리나 리스트에만 쓸 수 있습니다");
        }

        // ---- 클래스 생성자: 사람("미르", 15) ----
        auto cls = g_classes.find(name);
        if (cls != g_classes.end()) {
            Value obj;
            obj.kind = Value::OBJ;
            obj.className = name;
            obj.map = std::make_shared<std::map<string, Value>>();
            auto initIt = cls->second->methods.find("init");
            if (initIt != cls->second->methods.end()) {
                if (vals.size() != initIt->second->params.size())
                    throw err(name + "() 생성자는 인자 " + std::to_string(initIt->second->params.size())
                              + "개가 필요합니다 (지금 " + std::to_string(vals.size()) + "개)");
                runMethod(cls->second, initIt->second, obj, vals, line);
            } else if (!vals.empty()) {
                throw err("클래스 '" + name + "' 에 init 이 없어서 인자를 받을 수 없습니다");
            }
            return obj;
        }

        // ---- 사용자 정의 함수 ----
        auto it = g_funcs.find(name);
        if (it == g_funcs.end()) {
            std::vector<string> names = BUILTIN_NAMES;
            for (auto& [k, v] : g_funcs)   names.push_back(k);
            for (auto& [k, v] : g_classes) names.push_back(k);
            throw err("정의되지 않은 함수 또는 클래스: " + name + suggestName(name, names));
        }
        FuncStmt* fn = it->second;
        if (vals.size() != fn->params.size())
            throw err(name + "() 는 인자 " + std::to_string(fn->params.size())
                      + "개가 필요합니다 (지금 " + std::to_string(vals.size()) + "개)");
        DepthGuard guard(line, name);   // 무한 재귀 방지 + 호출 경로
        Env local;
        local.parent = g_global;
        // vals 는 여기서 끝이라 옮겨 담는다 — Value 하나에 문자열 둘과 shared_ptr 둘이 들어
        // 있어서 복사가 공짜가 아니다 (인자 하나당 130ns 쯤이 여기서 나왔다)
        for (size_t i = 0; i < vals.size(); i++)
            local.define(fn->params[i], std::move(vals[i]));
        if (fn->body->exec(local) == Flow::RETURN) return g_retVal;
        return Value::number(0);
    }
};

Value runMethod(ClassStmt* cls, FuncStmt* fn, Value& self,
                std::vector<Value>& args, int line) {
    (void)cls;
    DepthGuard guard(line, fn->name);
    Env local;
    local.parent = g_global;
    local.define("self", self);   // self 는 같은 필드 맵을 공유 → 수정이 원본에 반영
    // 부르는 쪽(MethodCallExpr, 생성자)이 args 를 이 호출 뒤로 쓰지 않으므로 옮겨 담는다
    for (size_t i = 0; i < args.size(); i++)
        local.define(fn->params[i], std::move(args[i]));
    if (fn->body->exec(local) == Flow::RETURN) return g_retVal;
    return Value::number(0);
}

// ============================================================
//  4. 파서 — 토큰 → AST (재귀 하강)
//
//  program   = statement*
//  statement = "let" IDENT ("=" expr)?
//            | IDENT ("="|"+="|"-="|"*="|"/=") expr
//            | IDENT ("[" expr "]")+ "=" expr
//            | IDENT "(" args ")"
//            | "print" expr ("," expr)*
//            | "if" expr "then"? block ("else" (ifstmt | block))?
//            | "while" expr "do"? block
//            | "for" IDENT "=" expr "to" expr ("step" expr)? block
//            | "func" IDENT "(" params ")" block
//            | "return" expr? | "break" | "continue"
//  expr      = or
//  or        = and ("or" and)*
//  and       = notExpr ("and" notExpr)*
//  notExpr   = "not" notExpr | comparison
//  comparison= addsub (("=="|"!="|"<"|">"|"<="|">=") addsub)*
//  addsub    = muldiv (("+"|"-") muldiv)*
//  muldiv    = unary (("*"|"/"|"%") unary)*
//  unary     = "-" unary | postfix
//  postfix   = primary ("[" expr "]")*
//  primary   = NUMBER | STRING | IDENT | IDENT "(" args ")"
//            | "[" (expr ("," expr)*)? "]" | "input" STRING? | "(" expr ")"
// ============================================================

// 재귀 하강 파서의 중첩 깊이 제한.
//
// 이게 없으면 "((((((...1...))))))" 같은 입력에 파서가 그대로 재귀해 스택을 넘기고
// 세그폴트로 죽는다 (퍼저가 찾았다). 인터프리터는 128MB 스택 스레드 위에서 돌아
// 수만 단계까지 버티지만 topython·build 는 메인 스레드에서 파싱하므로 5천 단계에
// 이미 죽었다 — 같은 파일이 실행은 되는데 변환만 죽는 상태였다.
//
// 깊이를 여기서 막으면 AST 깊이가 같이 묶여서 eval·CodeGen·PyGen·소멸자 재귀까지
// 한 번에 안전해진다. 교과서 코드는 10단계도 안 쓴다.
//
// 보간 "{...}" 안은 별도 Parser 로 파싱되므로 카운터는 전역이어야 한다.
static const int MAX_NEST = 200;
static int g_parseNest = 0;
struct NestGuard {
    explicit NestGuard(int line) {
        if (++g_parseNest > MAX_NEST) {
            --g_parseNest;
            throw LangError(lineTag(line) + "식이나 블록이 너무 깊게 중첩되었습니다 ("
                            + std::to_string(MAX_NEST) + "단계 초과)"
                            + " — 괄호가 제대로 닫혔는지 확인하세요");
        }
    }
    ~NestGuard() { --g_parseNest; }
};

struct Parser {
    std::vector<Token> toks;
    size_t pos = 0;
    bool inClassBody = false;   // 메서드 이름만은 내장 함수와 겹쳐도 된다 (obj.len() 로 부르니까)
    Parser(std::vector<Token> t) : toks(std::move(t)) {}

    const Token& peek(size_t ahead = 0) {
        size_t i = pos + ahead;
        return toks[i < toks.size() ? i : toks.size() - 1];
    }
    Token advance() { return toks[pos++]; }
    bool check(Tok t) { return peek().type == t; }
    bool match(Tok t) { if (check(t)) { pos++; return true; } return false; }
    Token expect(Tok t, const string& what) {
        if (!check(t))
            throw LangError(lineTag(peek().line) + "문법 오류: " + what
                            + josa(what, "이", "가") + " 필요합니다");
        return advance();
    }
    // 다음 토큰이 표현식의 시작이 될 수 있는가? (값 없는 return 판별용)
    bool startsExpr() {
        switch (peek().type) {
            case Tok::NUMBER: case Tok::STRING: case Tok::IDENT:
            case Tok::INPUT:  case Tok::LBRACKET: case Tok::LPAREN:
            case Tok::MINUS:  case Tok::NOT:
                return true;
            default: return false;
        }
    }

    std::vector<StmtP> parseProgram() {
        g_parseNest = 0;
        std::vector<StmtP> out;
        while (!check(Tok::END)) out.push_back(parseStatement());
        return out;
    }

    StmtP parseStatement() {
        int line = peek().line;
        if (match(Tok::LET)) {
            Token name = expect(Tok::IDENT, "변수 이름");
            if (match(Tok::ASSIGN))
                return std::make_unique<LetStmt>(name.text, parseExpr());
            return std::make_unique<LetStmt>(name.text, std::make_unique<NumExpr>(0));
        }
        if (match(Tok::PRINT)) {
            auto node = std::make_unique<PrintStmt>();
            node->vals.push_back(parseExpr());
            while (match(Tok::COMMA))
                node->vals.push_back(parseExpr());
            return node;
        }
        if (match(Tok::IF)) {
            auto node = std::make_unique<IfStmt>();
            node->cond = parseExpr();
            checkCompareTypo(KW_IF);
            match(Tok::FILLER);
            node->thenB = parseBlock();
            if (match(Tok::ELSE)) {
                if (check(Tok::IF)) node->elseB = parseStatement();  // else if 체인
                else                node->elseB = parseBlock();
            }
            return node;
        }
        if (match(Tok::WHILE)) {
            auto node = std::make_unique<WhileStmt>();
            node->cond = parseExpr();
            checkCompareTypo(KW_WHILE);
            match(Tok::FILLER);
            node->body = parseBlock();
            return node;
        }
        if (match(Tok::FOR)) {
            Token varTok = expect(Tok::IDENT, "반복 변수 이름");
            if (match(Tok::INKW)) {                       // for x in xs { }
                auto node = std::make_unique<ForEachStmt>();
                node->line = line;
                node->var = varTok.text;
                node->iter = parseExpr();
                node->body = parseBlock();
                return node;
            }
            auto node = std::make_unique<ForStmt>();
            node->line = line;
            node->var = varTok.text;
            expect(Tok::ASSIGN, "=");
            node->start = parseExpr();
            expect(Tok::TO, KW_TO);
            node->end = parseExpr();
            if (match(Tok::STEP)) node->step = parseExpr();
            node->body = parseBlock();
            return node;
        }
        if (match(Tok::TRY)) {
            auto node = std::make_unique<TryStmt>();
            node->tryB = parseBlock();
            expect(Tok::CATCH, KW_CATCH);
            node->var = expect(Tok::IDENT, "에러를 담을 변수 이름").text;
            node->catchB = parseBlock();
            return node;
        }
        if (match(Tok::CLASS)) {
            auto node = std::make_unique<ClassStmt>();
            Token nameTok = expect(Tok::IDENT, "클래스 이름");
            node->name = nameTok.text;
            if (isBuiltinName(node->name))
                throw LangError(lineTag(nameTok.line) + "클래스 이름 '" + node->name
                                + "' 은 내장 함수와 겹칩니다 (다른 이름을 쓰세요)");
            expect(Tok::LBRACE, "{");
            bool wasInClass = inClassBody;
            inClassBody = true;                 // 메서드 이름은 내장과 겹쳐도 된다 (obj.len() 로 부른다)
            while (!check(Tok::RBRACE) && !check(Tok::END)) {
                if (!check(Tok::FUNC))
                    throw LangError(lineTag(peek().line) + "클래스 안에는 " + KW_FUNC + " (메서드)만 쓸 수 있습니다");
                StmtP m = parseStatement();
                auto* fp = static_cast<FuncStmt*>(m.release());
                node->methods[fp->name] = fp;
                node->methodList.emplace_back(fp);
            }
            inClassBody = wasInClass;
            expect(Tok::RBRACE, "}");
            return node;
        }
        if (match(Tok::FUNC)) {
            auto node = std::make_unique<FuncStmt>();
            Token nameTok = expect(Tok::IDENT, "함수 이름");
            node->name = nameTok.text;
            if (!inClassBody && isBuiltinName(node->name))
                throw LangError(lineTag(nameTok.line) + "함수 이름 '" + node->name
                                + "' 은 내장 함수와 겹칩니다 (다른 이름을 쓰세요 — 내장 함수는 가릴 수 없습니다)");
            expect(Tok::LPAREN, "(");
            if (!check(Tok::RPAREN)) {
                do {
                    if (check(Tok::RPAREN)) break;          // 마지막 쉼표 허용
                    node->params.push_back(expect(Tok::IDENT, "인자 이름").text);
                } while (match(Tok::COMMA));
            }
            expect(Tok::RPAREN, ")");
            node->body = parseBlock();
            return node;
        }
        if (match(Tok::RETURN)) {
            auto node = std::make_unique<ReturnStmt>();
            if (startsExpr())         // "return" 만 쓰면 0 반환
                node->val = parseExpr();
            return node;
        }
        if (match(Tok::BREAK))    return std::make_unique<BreakStmt>();
        if (match(Tok::CONTINUE)) return std::make_unique<ContinueStmt>();

        if (check(Tok::IDENT)) {
            // 일반 표현식으로 먼저 파싱: x, x[i], obj.필드, obj.메서드(), f() 전부 포함
            ExprP e = parseExpr();

            // 표현식을 "루트 변수 + 접근자 경로"로 분해 (대입 대상 판별용)
            std::vector<Accessor> rev;
            Expr* cur = e.get();
            while (true) {
                if (auto* ix = dynamic_cast<IndexExpr*>(cur)) {
                    Accessor a;
                    a.isField = false;
                    a.index = std::move(ix->index);
                    a.line = ix->line;
                    rev.push_back(std::move(a));
                    ExprP t = std::move(ix->target);
                    e = std::move(t);
                    cur = e.get();
                    continue;
                }
                if (auto* f = dynamic_cast<FieldExpr*>(cur)) {
                    Accessor a;
                    a.isField = true;
                    a.field = f->field;
                    a.line = f->line;
                    rev.push_back(std::move(a));
                    ExprP t = std::move(f->target);
                    e = std::move(t);
                    cur = e.get();
                    continue;
                }
                break;
            }
            auto* root = dynamic_cast<VarExpr*>(e.get());
            bool assignable = (root != nullptr);
            std::vector<Accessor> path;
            for (auto it = rev.rbegin(); it != rev.rend(); ++it)
                path.push_back(std::move(*it));

            auto isAssignTok = [&]() {
                return check(Tok::ASSIGN) || check(Tok::PLUSEQ) || check(Tok::MINUSEQ)
                    || check(Tok::STAREQ) || check(Tok::SLASHEQ);
            };
            if (isAssignTok()) {
                if (!assignable)
                    throw LangError(lineTag(line) + "여기에는 대입할 수 없습니다");
                string rootName = root->name;
                if (path.empty()) {                       // 단순 변수 대입/복합대입
                    auto compound = [&](Tok binOp) -> StmtP {
                        ExprP rhs2 = parseExpr();
                        ExprP self = std::make_unique<VarExpr>(rootName, line);
                        ExprP combined = std::make_unique<BinExpr>(binOp, std::move(self), std::move(rhs2), line);
                        return std::make_unique<AssignStmt>(rootName, std::move(combined), line);
                    };
                    if (match(Tok::PLUSEQ))  return compound(Tok::PLUS);
                    if (match(Tok::MINUSEQ)) return compound(Tok::MINUS);
                    if (match(Tok::STAREQ))  return compound(Tok::STAR);
                    if (match(Tok::SLASHEQ)) return compound(Tok::SLASH);
                    expect(Tok::ASSIGN, "=");
                    return std::make_unique<AssignStmt>(rootName, parseExpr(), line);
                }
                auto pcompound = [&](Tok binOp) -> StmtP {   // 경로 복합 대입
                    auto node = std::make_unique<PathCompoundStmt>();
                    node->name = rootName;
                    node->path = std::move(path);
                    node->op = binOp;
                    node->line = line;
                    node->rhs = parseExpr();
                    return node;
                };
                if (match(Tok::PLUSEQ))  return pcompound(Tok::PLUS);
                if (match(Tok::MINUSEQ)) return pcompound(Tok::MINUS);
                if (match(Tok::STAREQ))  return pcompound(Tok::STAR);
                if (match(Tok::SLASHEQ)) return pcompound(Tok::SLASH);
                expect(Tok::ASSIGN, "=");
                auto node = std::make_unique<PathAssignStmt>();
                node->name = rootName;
                node->path = std::move(path);
                node->line = line;
                node->val = parseExpr();
                return node;
            }
            // 대입이 아니면: 호출 문장만 허용 (경로가 있으면 원래 표현식으로 복원 불가하므로 검사 먼저)
            if (!path.empty())
                throw LangError(lineTag(line) + "문법 오류: = 이(가) 필요합니다");
            if (dynamic_cast<CallExpr*>(e.get()) || dynamic_cast<MethodCallExpr*>(e.get()))
                return std::make_unique<ExprStmt>(std::move(e));
            // "elif x == 2 {" 나 "def f():" 는 여기로 떨어진다 — 그냥 "= 가 필요합니다"
            // 라고 하면 학생은 자기가 어느 언어의 버릇을 썼는지 모른다.
            throw LangError(lineTag(line) + "문법 오류: = 이(가) 필요합니다"
                            + (root ? foreignHint(root->name) : string()));
        }
        if (check(Tok::INKW))
            throw LangError(lineTag(peek().line) + KW_IN + " 은 for 반복에서만 씁니다"
                            " (리스트에 있는지 보려면 has(리스트, 값))");
        throw LangError(lineTag(line) + "문법 오류: 문장이 될 수 없는 토큰입니다");
    }

    StmtP parseBlock() {
        int openLine = peek().line;
        NestGuard g(openLine);          // if 안에 if 안에 if ... 로 깊어지는 쪽
        if (check(Tok::COLON))
            throw LangError(lineTag(openLine) + "Venos 는 들여쓰기가 아니라 { } 로 묶습니다"
                            " (: 를 지우고 { 로 열어서 } 로 닫으세요)");
        expect(Tok::LBRACE, "{");
        auto block = std::make_unique<BlockStmt>();
        while (!check(Tok::RBRACE) && !check(Tok::END))
            block->stmts.push_back(parseStatement());
        // 파일 끝까지 } 가 안 나왔다 — 끝 줄이 아니라 열린 자리를 가리켜야 찾을 수 있다
        if (!check(Tok::RBRACE))
            throw LangError(lineTag(openLine) + "여기서 연 { 를 닫는 } 가 없습니다");
        advance();
        return block;
    }
    // if/while 조건 뒤에 = 가 오면 == 를 잘못 쓴 것이다 (초보자 최빈 실수)
    void checkCompareTypo(const string& kw) {
        if (check(Tok::ASSIGN))
            throw LangError(lineTag(peek().line) + kw + " 조건에서 값을 견줄 때는 == 를 씁니다"
                                                       " (= 는 값을 넣을 때)");
    }

    ExprP parseExpr() { return parseOr(); }

    ExprP parseOr() {
        ExprP left = parseAnd();
        while (match(Tok::OR))
            left = std::make_unique<LogicalExpr>(Tok::OR, std::move(left), parseAnd());
        return left;
    }
    ExprP parseAnd() {
        ExprP left = parseNot();
        while (match(Tok::AND))
            left = std::make_unique<LogicalExpr>(Tok::AND, std::move(left), parseNot());
        return left;
    }
    ExprP parseNot() {
        if (check(Tok::NOT)) {
            NestGuard g(peek().line);   // not not not ... 은 여기서만 깊어진다
            advance();
            return std::make_unique<NotExpr>(parseNot());
        }
        return parseComparison();
    }
    ExprP parseComparison() {
        ExprP left = parseAddSub();
        if (check(Tok::EQ) || check(Tok::NEQ) || check(Tok::LT)
         || check(Tok::GT) || check(Tok::LE)  || check(Tok::GE)) {
            Token op = advance();
            left = std::make_unique<BinExpr>(op.type, std::move(left), parseAddSub(), op.line);
            // a < b < c 는 (a<b)<c 로 조용히 오작동하므로 명시적으로 막는다
            if (check(Tok::EQ) || check(Tok::NEQ) || check(Tok::LT)
             || check(Tok::GT) || check(Tok::LE)  || check(Tok::GE))
                throw LangError(lineTag(peek().line)
                    + "비교 연산은 연결해서 쓸 수 없습니다 (a < b < c 대신 a < b and b < c)");
        }
        return left;
    }
    ExprP parseAddSub() {
        ExprP left = parseMulDiv();
        while (check(Tok::PLUS) || check(Tok::MINUS)) {
            Token op = advance();
            left = std::make_unique<BinExpr>(op.type, std::move(left), parseMulDiv(), op.line);
        }
        return left;
    }
    ExprP parseMulDiv() {
        ExprP left = parseUnary();
        while (check(Tok::STAR) || check(Tok::SLASH) || check(Tok::PERCENT)) {
            Token op = advance();
            left = std::make_unique<BinExpr>(op.type, std::move(left), parseUnary(), op.line);
        }
        return left;
    }
    // 괄호·리스트·딕셔너리·인덱스·호출 인자 — 식이 한 겹 깊어지는 길은 모두
    // 여기를 한 번씩 지난다. 단항 빼기(-)의 자기 재귀도 같이 막힌다.
    ExprP parseUnary() {
        NestGuard g(peek().line);
        if (check(Tok::MINUS)) {
            int line = peek().line;
            advance();
            return std::make_unique<NegExpr>(parseUnary(), line);
        }
        return parsePostfix();
    }
    ExprP parsePostfix() {
        ExprP e = parsePrimary();
        while (true) {
            if (check(Tok::LBRACKET)) {
                int line = peek().line;
                advance();
                ExprP idx = parseExpr();
                if (check(Tok::COLON))
                    throw LangError(lineTag(peek().line) + "Venos 에는 xs[1:3] 같은 슬라이스가 없습니다"
                                    " (문자열은 substr(s, 시작, 개수), 리스트는 반복문으로)");
                expect(Tok::RBRACKET, "]");
                e = std::make_unique<IndexExpr>(std::move(e), std::move(idx), line);
                continue;
            }
            if (check(Tok::DOT)) {
                int line = peek().line;
                advance();
                Token nameTok = expect(Tok::IDENT, "필드/메서드 이름");
                if (match(Tok::LPAREN)) {          // obj.메서드(인자)
                    auto mc = std::make_unique<MethodCallExpr>();
                    mc->target = std::move(e);
                    mc->method = nameTok.text;
                    mc->line = line;
                    if (!check(Tok::RPAREN)) {
                        do {
                            if (check(Tok::RPAREN)) break;  // 마지막 쉼표 허용
                            mc->args.push_back(parseExpr());
                        } while (match(Tok::COMMA));
                    }
                    expect(Tok::RPAREN, ")");
                    e = std::move(mc);
                } else {                            // obj.필드
                    auto f = std::make_unique<FieldExpr>();
                    f->target = std::move(e);
                    f->field = nameTok.text;
                    f->line = line;
                    e = std::move(f);
                }
                continue;
            }
            break;
        }
        return e;
    }
    // "{식}" 문자열 보간 — "이름: {x}" 를  "" + "이름: " + (x) 연결식으로 디슈가링.
    // 파스 단계에서 일반 AST 가 되므로 인터프리터/트랜스파일러 양쪽에 자동 적용.
    // 규칙: {{ 는 진짜 { 하나, } 단독은 그냥 문자.
    ExprP buildStringExpr(const string& s, int line) {
        std::vector<ExprP> parts;
        string lit;
        size_t i = 0;
        bool interpolated = false;
        while (i < s.size()) {
            char c = s[i];
            if (c == '{') {
                if (i + 1 < s.size() && s[i + 1] == '{') { lit += '{'; i += 2; continue; }
                int depth = 1; size_t j = i + 1;
                while (j < s.size()) {
                    if (s[j] == '{') depth++;
                    else if (s[j] == '}' && --depth == 0) break;
                    j++;
                }
                if (depth != 0)
                    throw LangError(lineTag(line)
                        + "문자열 보간의 { 에 짝이 되는 } 가 없습니다"
                          " — {} 안에서 따옴표를 쓰면 문자열이 거기서 끝나 버립니다"
                          " (\\\" 로 쓰거나, 먼저 변수에 담으세요. 진짜 { 를 쓰려면 {{)");
                string inner = s.substr(i + 1, j - i - 1);
                if (trim(inner).empty())
                    throw LangError(lineTag(line) + "문자열 보간 {} 안이 비어 있습니다");
                if (!lit.empty()) { parts.push_back(std::make_unique<StrExpr>(lit)); lit.clear(); }
                try {
                    // 하위 파서는 {} 안쪽 조각만 보므로 줄 번호가 1부터 다시 시작한다.
                    // 그대로 두면 "{없는변수}" 의 실행 에러가 엉뚱한 줄을 가리킨다.
                    auto innerToks = lex(inner);
                    for (auto& t : innerToks) t.line = line;
                    Parser sub(innerToks);
                    ExprP e = sub.parseExpr();
                    if (!sub.check(Tok::END))
                        throw LangError("{} 안에는 식 하나만 들어갈 수 있습니다");
                    parts.push_back(std::move(e));
                } catch (LangError& inErr) {
                    // 왜 잘못됐는지까지 말해 준다. 하위 파서는 {} 안쪽 조각만 보므로
                    // 그쪽 [줄 1] 은 바깥 줄 번호와 어긋난다 — 떼어 내고 바깥 것을 쓴다.
                    string why = inErr.what();
                    size_t close = why.find("] ");
                    if (!why.empty() && why[0] == '[' && close != string::npos)
                        why = why.substr(close + 2);
                    throw LangError(lineTag(line) + "문자열 보간 {" + ellipsize(inner, 40)
                                    + "} 안의 식이 잘못되었습니다: " + why);
                }
                interpolated = true;
                i = j + 1;
                continue;
            }
            // }} 는 진짜 } 하나 ({{...}} 대칭 이스케이프). } 단독도 그냥 문자.
            if (c == '}' && i + 1 < s.size() && s[i + 1] == '}') { lit += '}'; i += 2; continue; }
            lit += c;
            i++;
        }
        if (!interpolated) return std::make_unique<StrExpr>(lit);
        if (!lit.empty()) parts.push_back(std::make_unique<StrExpr>(lit));
        // "" 로 시작해 어떤 조합이든 문자열 연결이 되게 한다 ("{a}{b}" 가 덧셈이 되지 않도록)
        ExprP out = std::make_unique<StrExpr>("");
        for (auto& p : parts)
            out = std::make_unique<BinExpr>(Tok::PLUS, std::move(out), std::move(p), line);
        static_cast<BinExpr*>(out.get())->interpN = (int)parts.size();
        return out;
    }

    ExprP parsePrimary() {
        Token t = peek();
        if (match(Tok::INPUT)) {
            string prompt;
            if (check(Tok::STRING)) prompt = advance().text;
            return std::make_unique<InputExpr>(prompt);
        }
        if (match(Tok::NUMBER)) return std::make_unique<NumExpr>(t.num);
        if (match(Tok::STRING)) return buildStringExpr(t.text, t.line);
        if (check(Tok::IDENT)) {
            if (peek(1).type == Tok::LPAREN) {
                Token name = advance();
                advance();  // (
                std::vector<ExprP> args;
                if (!check(Tok::RPAREN)) {
                    do {
                        if (check(Tok::RPAREN)) break;      // 마지막 쉼표 허용
                        args.push_back(parseExpr());
                    } while (match(Tok::COMMA));
                }
                expect(Tok::RPAREN, ")");
                return std::make_unique<CallExpr>(name.text, std::move(args), name.line);
            }
            advance();
            return std::make_unique<VarExpr>(t.text, t.line);
        }
        if (match(Tok::LBRACKET)) {
            auto list = std::make_unique<ListExpr>();
            if (!check(Tok::RBRACKET)) {
                // 마지막 쉼표를 허용한다 (여러 줄로 쓴 리스트에서 흔하고, 파이썬도 받아 준다)
                do {
                    if (check(Tok::RBRACKET)) break;
                    list->items.push_back(parseExpr());
                } while (match(Tok::COMMA));
            }
            expect(Tok::RBRACKET, "]");
            return list;
        }
        if (match(Tok::LBRACE)) {   // 딕셔너리 리터럴 {"키": 값, ...}
            auto m = std::make_unique<MapExpr>();
            m->line = t.line;
            if (!check(Tok::RBRACE)) {
                do {
                    if (check(Tok::RBRACE)) break;          // 마지막 쉼표 허용
                    ExprP k = parseExpr();
                    expect(Tok::COLON, ":");
                    ExprP v = parseExpr();
                    m->items.emplace_back(std::move(k), std::move(v));
                } while (match(Tok::COMMA));
            }
            expect(Tok::RBRACE, "}");
            return m;
        }
        if (match(Tok::LPAREN)) {
            ExprP e = parseExpr();
            expect(Tok::RPAREN, ")");
            return e;
        }
        throw LangError(lineTag(t.line) + "문법 오류: 값이 와야 할 자리입니다");
    }
};

// ============================================================
//  5. 실행기
// ============================================================
void runSource(const string& src) {
    auto tokens = lex(src);
    Parser parser(std::move(tokens));
    auto program = parser.parseProgram();
    g_funcs.clear();
    g_classes.clear();
    g_callDepth = 0;
    g_frames.clear();   // 앞 실행이 스택 넘침 등으로 중간에 끊겼으면 프레임이 남아 있다
    // 함수/클래스 호이스팅: 정의보다 위에서 사용하는 코드도 작동
    for (auto& stmt : program) {
        if (auto* fn = dynamic_cast<FuncStmt*>(stmt.get()))
            g_funcs[fn->name] = fn;
        if (auto* cs = dynamic_cast<ClassStmt*>(stmt.get()))
            g_classes[cs->name] = cs;
    }
    Env global;
    g_global = &global;
    try {
        for (auto& stmt : program) {
            Flow f = stmt->exec(global);
            if (f == Flow::NORMAL) continue;
            g_global = nullptr;
            throw LangError(f == Flow::BREAK    ? KW_BREAK + " 는 반복문 안에서만 쓸 수 있습니다"
                          : f == Flow::CONTINUE ? KW_CONTINUE + " 는 반복문 안에서만 쓸 수 있습니다"
                                                : KW_RETURN + " 은 함수 안에서만 쓸 수 있습니다");
        }
    } catch (ExitSignal&) {
        g_global = nullptr;
        return;                       // exit() = 정상 종료
    } catch (...) {
        g_global = nullptr;
        throw;
    }
    g_global = nullptr;
}

// ------------------------------------------------------------
// 재귀가 깊어도 스택이 터지지 않도록, 128MB 스택을 가진 전용
// 스레드에서 실행한다. (기본 스택: Windows 2MB / Linux 8MB 라서
// 레벨당 수 KB씩 쓰는 인터프리터 재귀가 금방 한계에 닿음.
// 이렇게 하면 MAX_RECURSION 제한이 스택보다 항상 먼저 걸려서
// 세그폴트 대신 깔끔한 에러 메시지가 나온다.)
// ------------------------------------------------------------
static void runOnBigStack(const std::function<void()>& job) {
#ifdef VENOS_WASM
    job();
    return;
#endif
    std::exception_ptr eptr = nullptr;
    auto work = [&]() {
        try { job(); }
        catch (...) { eptr = std::current_exception(); }
    };
    using Work = decltype(work);
    constexpr size_t STACK_BYTES = 128ull * 1024 * 1024;
#ifdef _WIN32
    auto tramp = [](void* p) -> unsigned {
        (*static_cast<Work*>(p))();
        return 0;
    };
    HANDLE th = (HANDLE)_beginthreadex(nullptr, (unsigned)STACK_BYTES,
                                       tramp, &work,
                                       STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    if (!th) { job(); return; }   // 스레드 생성 실패 시 그냥 직접 실행
    WaitForSingleObject(th, INFINITE);
    CloseHandle(th);
#else
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, STACK_BYTES);
    auto tramp = [](void* p) -> void* {
        (*static_cast<Work*>(p))();
        return nullptr;
    };
    pthread_t th;
    if (pthread_create(&th, &attr, tramp, &work) != 0) {
        pthread_attr_destroy(&attr);
        job();
        return;
    }
    pthread_join(th, nullptr);
    pthread_attr_destroy(&attr);
#endif
    if (eptr) std::rethrow_exception(eptr);
}
static void runSourceBigStack(const string& src) {
    runOnBigStack([&] { runSource(src); });
}

// ============================================================
//  5.5  CodeGen — Venos AST → C++ 소스 코드 (트랜스파일러)
//
//  같은 렉서/파서/AST를 재사용하고, eval/exec 대신
//  "그 일을 하는 C++ 코드 문자열"을 뽑아낸다.
//  생성된 .cpp 는 아래 RUNTIME(작은 런타임 라이브러리)을 앞에 붙여
//  인터프리터와 동일한 값/에러 의미를 유지한다.
// ============================================================
static const char* RUNTIME = R"RT(// ---- Venos 런타임 (자동 생성) ----
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <random>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <map>
#include <cstdio>
#include <filesystem>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#undef IN
#undef OUT
#endif
using std::string;
struct Value; using List = std::vector<Value>;
struct Value {
    enum Kind { NUM, STR, LIST, MAP, OBJ } kind = NUM;
    double num = 0; string str;
    std::shared_ptr<List> list;
    std::shared_ptr<std::map<string, Value>> map;   // MAP 항목 / OBJ 필드
    string className;
    Value() {}
    Value(double d) : kind(NUM), num(d) {}
    Value(const string& s) : kind(STR), str(s) {}
    bool truthyV() const {
        if (kind == NUM) return num != 0;
        if (kind == STR) return !str.empty();
        if (kind == MAP) return map && !map->empty();
        if (kind == OBJ) return true;
        return list && !list->empty();
    }
    string kindName() const {
        return kind==NUM?"숫자":kind==STR?"문자열":kind==MAP?"딕셔너리"
             : kind==OBJ?"객체":"리스트";
    }
    string toString(int depth = 0) const;
};
using Map = std::map<string, Value>;
// 본체의 LangError 와 같은 이유로 문구를 넉넉히 잘라 둔다 (3천 자짜리 이름이 들어와도
// 에러 한 줄이 화면을 덮지 않게). 글자 경계에서 자른다.
static string rt_cut(const string& s, size_t limit) {
    size_t i = 0, n = 0;
    while (i < s.size() && n < limit) { i++; while (i < s.size() && (s[i] & 0xC0) == 0x80) i++; n++; }
    return i >= s.size() ? s : s.substr(0, i) + " \u2026";
}
struct RunErr : std::runtime_error { RunErr(const string& m) : std::runtime_error(rt_cut(m, 300)) {} };
string Value::toString(int depth) const {
    if (kind == STR) return str;
    if (depth > 1000)
        throw RunErr("출력할 수 없습니다 (자기 자신을 포함한 구조?)");
    if (kind == LIST) {
        string o = "[";
        for (size_t i = 0; i < list->size(); i++) {
            if (i) o += ", ";
            const Value& e = (*list)[i];
            o += (e.kind == STR) ? "\"" + e.str + "\"" : e.toString(depth + 1);
        }
        return o + "]";
    }
    if (kind == MAP) {
        string o = "{"; bool first = true;
        for (auto& [k, v] : *map) {
            if (!first) o += ", ";
            first = false;
            o += "\"" + k + "\": ";
            o += (v.kind == STR) ? "\"" + v.str + "\"" : v.toString(depth + 1);
        }
        return o + "}";
    }
    if (kind == OBJ) {
        string o = className + "{"; bool first = true;
        for (auto& [k, v] : *map) {
            if (!first) o += ", ";
            first = false;
            o += "\"" + k + "\": ";
            o += (v.kind == STR) ? "\"" + v.str + "\"" : v.toString(depth + 1);
        }
        return o + "}";
    }
    if (std::fabs(num) < 9.0e18 && num == (long long)num)
        return std::to_string((long long)num);
    std::ostringstream os; os << num; return os.str();
}
static bool truthy(const Value& v) { return v.truthyV(); }
static std::vector<string> u8chars(const string& s) {
    std::vector<string> out; size_t i = 0;
    while (i < s.size()) {
        unsigned char c = s[i]; size_t len = 1;
        if      ((c & 0x80) == 0x00) len = 1;
        else if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        if (i + len > s.size()) len = 1;
        out.push_back(s.substr(i, len)); i += len;
    }
    return out;
}
// 한국어 조사 (인터프리터와 같은 문구를 내기 위해 그대로 옮겨 온다)
static string josa(const string& word, const char* withJong, const char* without) {
    auto cs = u8chars(word);
    if (!cs.empty() && cs.back().size() == 3) {
        const string& last = cs.back();
        unsigned cp = ((unsigned char)last[0] & 0x0Fu) << 12
                    | ((unsigned char)last[1] & 0x3Fu) << 6
                    | ((unsigned char)last[2] & 0x3Fu);
        if (cp >= 0xAC00 && cp <= 0xD7A3)
            return ((cp - 0xAC00) % 28) ? withJong : without;
    }
    return " " + string(withJong) + "(" + without + ")";   // 기호·영어는 띄어서 둘 다
}
static List iter_items(const Value& v) {
    if (v.kind == Value::LIST) return *v.list;
    if (v.kind == Value::STR) { List o; for (auto& c : u8chars(v.str)) o.push_back(Value(c)); return o; }
    if (v.kind == Value::MAP) { List o; for (auto& kv : *v.map) o.push_back(Value(kv.first)); return o; }
    throw RunErr("for ... in 은 리스트/문자열/딕셔너리만 순회할 수 있습니다 (지금: " + v.kindName() + ")");
}
static Value mk_list(std::initializer_list<Value> xs) {
    Value v; v.kind = Value::LIST; v.list = std::make_shared<List>(xs); return v;
}
static Value mk_map(std::initializer_list<std::pair<Value, Value>> xs) {
    Value v; v.kind = Value::MAP; v.map = std::make_shared<Map>();
    for (auto& p : xs) {
        if (p.first.kind != Value::STR)
            throw RunErr("딕셔너리 키는 문자열이어야 합니다 (지금: " + p.first.kindName() + ")");
        (*v.map)[p.first.str] = p.second;
    }
    return v;
}
static double needNum(const Value& v, const char* what) {
    if (v.kind != Value::NUM) throw RunErr(string(what) + ": 숫자가 필요합니다 (지금: " + v.kindName() + ")");
    return v.num;
}
// 산술 이항 연산의 타입 검사 — 인터프리터와 동일한 에러 문구
static void needNums(const Value& a, const Value& b) {
    if (a.kind != Value::NUM || b.kind != Value::NUM)
        throw RunErr(a.kindName() + josa(a.kindName(), "과", "와") + " " + b.kindName()
                     + josa(b.kindName(), "은", "는") + " 이 연산이 안 됩니다");
}
static Value vadd(const Value& a, const Value& b) {
    if (a.kind == Value::STR || b.kind == Value::STR) return Value(a.toString() + b.toString());
    if (a.kind == Value::LIST && b.kind == Value::LIST) {   // 리스트 이어붙이기
        Value v; v.kind = Value::LIST;
        v.list = std::make_shared<List>(*a.list);
        v.list->insert(v.list->end(), b.list->begin(), b.list->end());
        return v;
    }
    needNums(a, b);
    return Value(a.num + b.num);
}
static Value vsub(const Value& a, const Value& b) { needNums(a, b); return Value(a.num - b.num); }
static const size_t RT_REPEAT_CAP = 10000000;
static Value vmul(const Value& a, const Value& b) {
    // 문자열 * 숫자 = 그만큼 반복 — 별 찍기, 막대그래프, 구분선
    if ((a.kind == Value::STR) != (b.kind == Value::STR)) {
        const Value& s = (a.kind == Value::STR) ? a : b;
        const Value& n = (a.kind == Value::STR) ? b : a;
        if (n.kind != Value::NUM)
            throw RunErr("문자열은 숫자만큼만 반복할 수 있습니다 (지금: " + n.kindName() + ")");
        if (n.num != std::floor(n.num))
            throw RunErr("문자열을 소수 번 반복할 수는 없습니다 (지금: " + Value(n.num).toString() + ")");
        size_t k = n.num <= 0 ? 0 : (size_t)n.num;
        if (n.num > (double)RT_REPEAT_CAP || s.str.size() * k > RT_REPEAT_CAP)
            throw RunErr("문자열 반복이 너무 깁니다 (최대 10000000자)");
        string out; out.reserve(s.str.size() * k);
        for (size_t i = 0; i < k; i++) out += s.str;
        return Value(out);
    }
    needNums(a, b);
    return Value(a.num * b.num);
}
static Value vdiv(const Value& a, const Value& b) {
    needNums(a, b);
    if (b.num == 0) throw RunErr("0으로 나눌 수 없습니다");
    return Value(a.num / b.num);
}
static Value vmod(const Value& a, const Value& b) {
    needNums(a, b);
    if (b.num == 0) throw RunErr("0으로 나머지 연산을 할 수 없습니다");
    // 나머지는 나누는 수의 부호를 따른다 (수학·파이썬 관례) — -7 % 3 = 2
    double r = std::fmod(a.num, b.num);
    if (r != 0 && ((r < 0) != (b.num < 0))) r += b.num;
    return Value(r);
}
static Value vneg(const Value& a) {
    if (a.kind != Value::NUM) throw RunErr(a.kindName() + "에는 - 를 붙일 수 없습니다");
    return Value(-a.num);
}
template<class F> static Value vcmp(const Value& a, const Value& b, F f) {
    if (a.kind == Value::LIST || b.kind == Value::LIST || a.kind == Value::MAP || b.kind == Value::MAP
     || a.kind == Value::OBJ  || b.kind == Value::OBJ)
        throw RunErr("리스트/딕셔너리/객체는 비교 연산을 지원하지 않습니다");
    if (a.kind != b.kind) throw RunErr("숫자와 문자열은 비교할 수 없습니다"
                                      "  (숫자처럼 보이는 문자열이면 num() 으로 바꿔 쓰세요)");
    bool r = (a.kind == Value::NUM) ? f(a.num, b.num) : f(a.str, b.str);
    return Value(r ? 1.0 : 0.0);
}
// ==/!= 깊은 비교 — 인터프리터의 deepEquals 와 동일 규칙 (타입 다르면 false, 순환은 깊이 한도)
static bool veqDeep(const Value& a, const Value& b, int depth) {
    if (depth > 1000) throw RunErr("비교할 수 없습니다 (자기 자신을 포함한 구조?)");
    if (a.kind != b.kind) return false;
    if (a.kind == Value::NUM) return a.num == b.num;
    if (a.kind == Value::STR) return a.str == b.str;
    if (a.kind == Value::LIST) {
        if (a.list == b.list) return true;
        if (a.list->size() != b.list->size()) return false;
        for (size_t i = 0; i < a.list->size(); i++)
            if (!veqDeep((*a.list)[i], (*b.list)[i], depth + 1)) return false;
        return true;
    }
    if (a.map == b.map) return true;
    if (a.className != b.className) return false;
    if (a.map->size() != b.map->size()) return false;
    auto ia = a.map->begin(), ib = b.map->begin();
    for (; ia != a.map->end(); ++ia, ++ib) {
        if (ia->first != ib->first) return false;
        if (!veqDeep(ia->second, ib->second, depth + 1)) return false;
    }
    return true;
}
static Value c_eq(const Value&a,const Value&b){ return Value(veqDeep(a,b,0) ? 1.0 : 0.0); }
static Value c_ne(const Value&a,const Value&b){ return Value(veqDeep(a,b,0) ? 0.0 : 1.0); }
static Value c_lt(const Value&a,const Value&b){ return vcmp(a,b,[](auto x,auto y){return x< y;}); }
static Value c_gt(const Value&a,const Value&b){ return vcmp(a,b,[](auto x,auto y){return x> y;}); }
static Value c_le(const Value&a,const Value&b){ return vcmp(a,b,[](auto x,auto y){return x<=y;}); }
static Value c_ge(const Value&a,const Value&b){ return vcmp(a,b,[](auto x,auto y){return x>=y;}); }
static size_t chkIdx(const Value& i, size_t size) {
    if (i.kind != Value::NUM) throw RunErr("인덱스는 숫자여야 합니다");
    if (i.num != std::floor(i.num)) throw RunErr("인덱스는 정수여야 합니다 (지금: " + i.toString() + ")");
    long long n = (long long)i.num;
    if (n < 1 || n > (long long)size)
        throw RunErr("인덱스 범위 초과: " + std::to_string(n) + " (리스트 크기: " + std::to_string(size) + ", 인덱스는 1부터)");
    return (size_t)(n - 1);
}
static const string& mapKey(const Value& i) {
    if (i.kind != Value::STR) throw RunErr("딕셔너리 키는 문자열이어야 합니다 (지금: " + i.kindName() + ")");
    return i.str;
}
static Value idx_get(const Value& t, const Value& i) {
    if (t.kind == Value::STR) {
        auto chars = u8chars(t.str);
        return Value(chars[chkIdx(i, chars.size())]);
    }
    if (t.kind == Value::MAP) {
        auto it = t.map->find(mapKey(i));
        if (it == t.map->end())
            throw RunErr("키가 없습니다: \"" + i.str + "\"  (has(딕셔너리, 키) 로 먼저 확인할 수 있어요)");
        return it->second;
    }
    if (t.kind != Value::LIST) throw RunErr(t.kindName() + "에는 [ ] 를 쓸 수 없습니다"
                                           + (t.kind == Value::OBJ ? "  (객체의 필드는 obj.이름 으로 씁니다)" : ""));
    return (*t.list)[chkIdx(i, t.list->size())];
}
// 인덱스 체인 중간 (반드시 존재해야 함) — 복합 대입의 마지막에도 사용
static Value& idx_mid(Value& t, const Value& i) {
    if (t.kind == Value::MAP) {
        auto it = t.map->find(mapKey(i));
        if (it == t.map->end()) throw RunErr("키가 없습니다: \"" + i.str + "\"");
        return it->second;
    }
    if (t.kind == Value::STR) throw RunErr("문자열의 글자는 직접 바꿀 수 없습니다 (replace() 를 쓰세요)");
    if (t.kind != Value::LIST) throw RunErr(t.kindName() + "에는 [ ] 를 쓸 수 없습니다"
                                           + (t.kind == Value::OBJ ? "  (객체의 필드는 obj.이름 으로 씁니다)" : ""));
    return (*t.list)[chkIdx(i, t.list->size())];
}
static Value fld_get(const Value& t, const string& f) {
    if (t.kind != Value::OBJ)
        throw RunErr(t.kindName() + "에는 . 필드를 쓸 수 없습니다 (딕셔너리는 [\"키\"] 를 쓰세요)");
    auto it = t.map->find(f);
    if (it == t.map->end()) throw RunErr("필드가 없습니다: ." + f);
    return it->second;
}
static Value& fld_mid(Value& t, const string& f) {
    if (t.kind != Value::OBJ)
        throw RunErr(t.kindName() + "에는 . 필드를 쓸 수 없습니다 (딕셔너리는 [\"키\"] 를 쓰세요)");
    auto it = t.map->find(f);
    if (it == t.map->end()) throw RunErr("필드가 없습니다: ." + f);
    return it->second;
}
static Value& fld_put(Value& t, const string& f) {
    if (t.kind != Value::OBJ)
        throw RunErr(t.kindName() + "에는 . 필드를 쓸 수 없습니다");
    return (*t.map)[f];
}
// 대입의 마지막 단계 — 딕셔너리는 새 키를 자동 생성
static Value& idx_put(Value& t, const Value& i) {
    if (t.kind == Value::MAP) return (*t.map)[mapKey(i)];
    if (t.kind == Value::STR) throw RunErr("문자열의 글자는 직접 바꿀 수 없습니다 (replace() 를 쓰세요)");
    if (t.kind != Value::LIST) throw RunErr(t.kindName() + "에는 [ ] 를 쓸 수 없습니다"
                                           + (t.kind == Value::OBJ ? "  (객체의 필드는 obj.이름 으로 씁니다)" : ""));
    return (*t.list)[chkIdx(i, t.list->size())];
}
static void my_print(std::initializer_list<string> vs) {
    string o; bool first = true;
    for (auto& v : vs) { if (!first) o += " "; first = false; o += v; }
    std::cout << o << "\n";
}
// 파일 경로 — 윈도우는 좁은 문자열 경로를 ANSI 코드페이지로 읽는다.
// 그대로 두면 readfile("자료.txt") 가 한글 이름을 못 찾는다 (인터프리터는 이미 이렇게 한다).
static std::filesystem::path rt_path(const string& utf8) {
#ifdef _WIN32
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), nullptr, 0);
    std::wstring w(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), &w[0], len);
    return std::filesystem::path(w);
#else
    return std::filesystem::path(utf8);
#endif
}
static string trimS(const string& s) {
    size_t a = s.find_first_not_of(" \t\r");
    if (a == string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r");
    return s.substr(a, b - a + 1);
}
// 한 줄 읽기 — Windows 콘솔은 UTF-8 getline 이 한글을 깨뜨려서 와이드로 읽음
static bool rt_readline(string& out) {
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;
    if (GetConsoleMode(h, &mode)) {
        wchar_t wbuf[4096];
        DWORD nRead = 0;
        if (!ReadConsoleW(h, wbuf, 4096, &nRead, nullptr)) return false;
        std::wstring ws(wbuf, nRead);
        while (!ws.empty() && (ws.back() == L'\n' || ws.back() == L'\r')) ws.pop_back();
        int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
        out.assign(len, '\0');
        WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), out.data(), len, nullptr, nullptr);
        return true;
    }
#endif
    return (bool)std::getline(std::cin, out);
}
// 문자열이 "학생이 숫자라고 생각하는 것"인가 (본체의 strToNum 과 같은 문법).
// std::stod 는 0x10·inf·nan 까지 받는데 파이썬의 float() 은 셋 다 거절한다 —
// 그대로 두면 세 백엔드가 다른 답을 낸다.
static bool rt_strToNum(const std::string& s, double& out) {
    size_t i = 0, n = s.size();
    auto skipSpace = [&] {
        while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n'
                         || s[i] == '\r' || s[i] == '\v' || s[i] == '\f')) i++;
    };
    auto digits = [&] {
        size_t k = 0;
        while (i < n && s[i] >= '0' && s[i] <= '9') { i++; k++; }
        return k;
    };
    skipSpace();
    if (i < n && (s[i] == '+' || s[i] == '-')) i++;
    size_t whole = digits(), frac = 0;
    if (i < n && s[i] == '.') { i++; frac = digits(); }
    if (whole == 0 && frac == 0) return false;
    if (i < n && (s[i] == 'e' || s[i] == 'E')) {
        i++;
        if (i < n && (s[i] == '+' || s[i] == '-')) i++;
        if (digits() == 0) return false;
    }
    skipSpace();
    if (i != n) return false;
    try { out = std::stod(s); } catch (...) { return false; }
    return out == out && out < HUGE_VAL && out > -HUGE_VAL;
}
static Value my_input(const string& prompt) {
    if (!prompt.empty()) std::cout << prompt << std::flush;
    string line;
    if (!rt_readline(line)) throw RunErr("입력을 읽을 수 없습니다");
    line = trimS(line);
    double d;
    if (rt_strToNum(line, d)) return Value(d);
    return Value(line);
}
static size_t u8len(const string& s) { size_t n = 0; for (unsigned char c : s) if ((c & 0xC0) != 0x80) n++; return n; }
static Value b_random(const Value& a, const Value& b) {
    long long x = (long long)needNum(a, "random"), y = (long long)needNum(b, "random");
    if (x > y) std::swap(x, y);
    static std::mt19937_64 rng{ std::random_device{}() };
    std::uniform_int_distribution<long long> d(x, y);
    return Value((double)d(rng));
}
static Value b_round(const Value& a) { return Value(std::round(needNum(a, "round"))); }
static Value b_round(const Value& a, const Value& b) {
    double x = needNum(a, "round"), d = needNum(b, "round");
    if (d != std::floor(d) || d < 0 || d > 15)
        throw RunErr("round() 의 자릿수는 0 이상 15 이하의 정수여야 합니다");
    double p = std::pow(10.0, d);
    return Value(std::round(x * p) / p);
}
static Value b_floor(const Value& a) { return Value(std::floor(needNum(a, "floor"))); }
static Value b_ceil (const Value& a) { return Value(std::ceil (needNum(a, "ceil" ))); }
static Value b_abs  (const Value& a) { return Value(std::fabs (needNum(a, "abs"  ))); }
static Value b_sqrt (const Value& a) {
    double x = needNum(a, "sqrt");
    if (x < 0) throw RunErr("sqrt() 에 음수는 넣을 수 없습니다");
    return Value(std::sqrt(x));
}
static Value b_min(const Value& a, const Value& b) { return Value(std::min(needNum(a,"min"), needNum(b,"min"))); }
static Value b_max(const Value& a, const Value& b) { return Value(std::max(needNum(a,"max"), needNum(b,"max"))); }
static Value b_num(const Value& v) {
    if (v.kind == Value::NUM) return v;
    if (v.kind == Value::STR) {
        double d;
        if (rt_strToNum(v.str, d)) return Value(d);
        throw RunErr("숫자로 바꿀 수 없는 문자열: \"" + rt_cut(v.str, 40) + "\"");
    }
    throw RunErr("리스트는 숫자로 바꿀 수 없습니다");
}
static Value b_str(const Value& v) { return Value(v.toString()); }
static Value b_len(const Value& v) {
    if (v.kind == Value::LIST) return Value((double)v.list->size());
    if (v.kind == Value::STR)  return Value((double)u8len(v.str));
    if (v.kind == Value::MAP)  return Value((double)v.map->size());
    throw RunErr("len() 은 리스트/문자열/딕셔너리에만 쓸 수 있습니다");
}
static Value b_push(Value a, const Value& b) {
    if (a.kind != Value::LIST) throw RunErr("push() 의 1번째 인자는 리스트여야 합니다");
    a.list->push_back(b);
    return a;
}
static Value b_pop(Value a) {
    if (a.kind != Value::LIST) throw RunErr("pop() 의 인자는 리스트여야 합니다");
    if (a.list->empty()) throw RunErr("빈 리스트에서는 pop() 할 수 없습니다");
    Value back = a.list->back();
    a.list->pop_back();
    return back;
}
static Value b_sort(Value a) {
    if (a.kind != Value::LIST) throw RunErr("sort() 의 인자는 리스트여야 합니다");
    auto& xs = *a.list;
    bool allNum = true, allStr = true;
    for (auto& x : xs) { if (x.kind != Value::NUM) allNum = false; if (x.kind != Value::STR) allStr = false; }
    if (!allNum && !allStr) throw RunErr("sort() 는 숫자만 있거나 문자열만 있는 리스트만 정렬할 수 있습니다");
    if (allNum) std::sort(xs.begin(), xs.end(), [](const Value& x, const Value& y) { return x.num < y.num; });
    else        std::sort(xs.begin(), xs.end(), [](const Value& x, const Value& y) { return x.str < y.str; });
    return a;
}
static const string& needStrR(const Value& v, const char* what) {
    if (v.kind != Value::STR) throw RunErr(string(what) + ": 문자열이 필요합니다 (지금: " + v.kindName() + ")");
    return v.str;
}
static Value b_split(const Value& a, const Value& b) {
    const string& s = needStrR(a, "split"); const string& sep = needStrR(b, "split");
    if (sep.empty()) throw RunErr("split() 의 구분자는 빈 문자열일 수 없습니다");
    Value out; out.kind = Value::LIST; out.list = std::make_shared<List>();
    size_t start = 0, p;
    while ((p = s.find(sep, start)) != string::npos) {
        out.list->push_back(Value(s.substr(start, p - start)));
        start = p + sep.size();
    }
    out.list->push_back(Value(s.substr(start)));
    return out;
}
static Value b_join(const Value& a, const Value& b) {
    if (a.kind != Value::LIST) throw RunErr("join() 의 1번째 인자는 리스트여야 합니다");
    const string& sep = needStrR(b, "join");
    string out;
    for (size_t i = 0; i < a.list->size(); i++) { if (i) out += sep; out += (*a.list)[i].toString(); }
    return Value(out);
}
static Value b_upper(const Value& a) { string s = needStrR(a, "upper"); for (auto& c : s) c = toupper((unsigned char)c); return Value(s); }
static Value b_lower(const Value& a) { string s = needStrR(a, "lower"); for (auto& c : s) c = tolower((unsigned char)c); return Value(s); }
static Value b_find(const Value& a, const Value& b) {
    if (a.kind == Value::LIST) {              // 리스트에서 값의 위치 (순차 탐색)
        auto& xs = *a.list;
        for (size_t i = 0; i < xs.size(); i++)
            if (veqDeep(xs[i], b, 0)) return Value((double)(i + 1));
        return Value(0.0);
    }
    auto hay = u8chars(needStrR(a, "find")), nee = u8chars(needStrR(b, "find"));
    if (nee.empty()) throw RunErr("find() 로 빈 문자열은 찾을 수 없습니다");
    if (nee.size() <= hay.size())
        for (size_t i = 0; i + nee.size() <= hay.size(); i++) {
            bool ok = true;
            for (size_t j = 0; j < nee.size(); j++) if (hay[i+j] != nee[j]) { ok = false; break; }
            if (ok) return Value((double)(i + 1));
        }
    return Value(0.0);
}
static Value b_replace(const Value& a, const Value& b, const Value& c) {
    string s = needStrR(a, "replace"); const string& from = needStrR(b, "replace"); const string& to = needStrR(c, "replace");
    if (from.empty()) throw RunErr("replace() 의 바꿀 문자열은 비어 있을 수 없습니다");
    string out; size_t start = 0, p;
    while ((p = s.find(from, start)) != string::npos) { out += s.substr(start, p - start); out += to; start = p + from.size(); }
    out += s.substr(start);
    return Value(out);
}
static Value b_substr(const Value& a, const Value& b, const Value& c) {
    auto chars = u8chars(needStrR(a, "substr"));
    double st = needNum(b, "substr"), cn = needNum(c, "substr");
    if (st != std::floor(st) || cn != std::floor(cn)) throw RunErr("substr() 의 시작/개수는 정수여야 합니다");
    long long start = (long long)st, count = (long long)cn;
    if (start < 1) throw RunErr("substr() 의 시작 위치는 1 이상이어야 합니다");
    if (count < 0) throw RunErr("substr() 의 개수는 0 이상이어야 합니다");
    string out;
    for (long long i = start - 1; i < (long long)chars.size() && i < start - 1 + count; i++) out += chars[i];
    return Value(out);
}
#include <fstream>
// 파일 열기 — 인터프리터와 같은 방식(넓은 경로, 바이너리 모드)이어야 한다.
// std::ifstream 의 filesystem::path 생성자는 MinGW 에서 없는 파일도 열린 것처럼 굴었다.
static std::FILE* rt_open(const string& path, const char* mode, const wchar_t* wmode) {
#ifdef _WIN32
    (void)mode;
    return _wfopen(rt_path(path).c_str(), wmode);
#else
    (void)wmode;
    return std::fopen(path.c_str(), mode);
#endif
}
static string rt_read_all(std::FILE* fp) {
    string out;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, fp)) > 0) out.append(buf, n);
    std::fclose(fp);
    return out;
}
static void rt_write_all(std::FILE* fp, const string& s) {
    if (!s.empty()) std::fwrite(s.data(), 1, s.size(), fp);
    std::fclose(fp);
}
static Value b_readfile(const Value& a) {
    std::FILE* fp = rt_open(needStrR(a, "readfile"), "rb", L"rb");
    if (!fp) throw RunErr("파일을 열 수 없습니다: " + a.str);
    return Value(rt_read_all(fp));
}
static Value b_writefile(const Value& a, const Value& b) {
    std::FILE* fp = rt_open(needStrR(a, "writefile"), "wb", L"wb");
    if (!fp) throw RunErr("파일을 만들 수 없습니다: " + a.str);
    rt_write_all(fp, b.toString());
    return Value(1.0);
}
#include <chrono>
#include <functional>
static Value b_time() {
    auto now = std::chrono::system_clock::now().time_since_epoch();
    return Value(std::chrono::duration<double>(now).count());
}
struct ExitSig {};
static Value b_exit() { throw ExitSig{}; }
static Value b_error(const Value& m) { throw RunErr(m.toString()); }
static Value rt_deepcopy(const Value& v, int depth) {
    if (depth > 1000) throw RunErr("복사할 수 없습니다 (자기 자신을 포함한 구조?)");
    if (v.kind == Value::LIST) {
        Value out; out.kind = Value::LIST; out.list = std::make_shared<List>();
        for (auto& e : *v.list) out.list->push_back(rt_deepcopy(e, depth + 1));
        return out;
    }
    if (v.kind == Value::MAP || v.kind == Value::OBJ) {
        Value out; out.kind = v.kind; out.className = v.className;
        out.map = std::make_shared<Map>();
        for (auto& kv : *v.map) (*out.map)[kv.first] = rt_deepcopy(kv.second, depth + 1);
        return out;
    }
    return v;
}
static Value b_copy(const Value& v) { return rt_deepcopy(v, 0); }
static Value b_exists(const Value& a) {
    std::error_code ec;
    return Value(std::filesystem::is_regular_file(rt_path(needStrR(a, "exists")), ec) ? 1.0 : 0.0);
}
static Value b_appendfile(const Value& a, const Value& b) {
    std::FILE* fp = rt_open(needStrR(a, "appendfile"), "ab", L"ab");
    if (!fp) throw RunErr("파일을 열 수 없습니다: " + a.str);
    rt_write_all(fp, b.toString());
    return Value(1.0);
}
static Value b_keys(const Value& v) {
    if (v.kind != Value::MAP) throw RunErr("keys() 의 인자는 딕셔너리여야 합니다");
    Value out; out.kind = Value::LIST; out.list = std::make_shared<List>();
    for (auto& kv : *v.map) out.list->push_back(Value(kv.first));
    return out;
}
static Value b_has(const Value& v, const Value& k) {
    if (v.kind == Value::LIST) {
        for (auto& x : *v.list) if (veqDeep(x, k, 0)) return Value(1.0);
        return Value(0.0);
    }
    if (v.kind != Value::MAP) throw RunErr("has() 의 1번째 인자는 딕셔너리나 리스트여야 합니다");
    return Value(v.map->count(mapKey(k)) ? 1.0 : 0.0);
}
static Value b_reverse(Value v) {
    if (v.kind == Value::LIST) { std::reverse(v.list->begin(), v.list->end()); return v; }
    if (v.kind != Value::STR)  throw RunErr("reverse() 의 인자는 리스트나 문자열이어야 합니다");
    auto cs = u8chars(v.str);
    string out;
    for (size_t i = cs.size(); i > 0; i--) out += cs[i - 1];
    return Value(out);
}
static Value b_remove(Value v, const Value& k) {
    if (v.kind == Value::MAP) return Value(v.map->erase(mapKey(k)) ? 1.0 : 0.0);
    if (v.kind == Value::LIST) {
        size_t i = chkIdx(k, v.list->size());
        Value removed = (*v.list)[i];
        v.list->erase(v.list->begin() + i);
        return removed;
    }
    throw RunErr("remove() 는 딕셔너리나 리스트에만 쓸 수 있습니다");
}
static int g_rdepth = 0;
struct DG {
    DG() { if (++g_rdepth > 2000) { --g_rdepth; throw RunErr("함수 호출이 너무 깊습니다 (재귀 2000회 초과 — 무한 재귀?)"); } }
    ~DG() { --g_rdepth; }
};
// ---- 런타임 끝, 아래부터 변환된 사용자 코드 ----
)RT";

struct CodeGen {
    std::ostringstream body;                 // main 본문
    std::ostringstream funcCode;             // 함수 정의들
    std::map<string, FuncStmt*> funcs;       // 이름 → 함수 (인자 개수 검사용)
    std::map<string, ClassStmt*> classes;    // 이름 → 클래스
    std::set<std::pair<string, int>> methodCalls;   // (메서드 이름, 인자 수) 사용 기록
    std::set<string> globalSet;              // 전역 변수 이름
    std::set<string> localSet;               // 현재 함수의 지역 변수 (인자 포함)
    bool inFunc = false;
    int loopDepth = 0;                       // break/continue 위치 검사
    int tmpN = 0;                            // for 임시 변수 고유 번호

    // 빌드 시점 검사용 내장 함수 표: 이름 → (인자 수, 런타임 함수 이름)
    std::map<string, std::pair<int, string>> builtins = {
        {"random", {2, "b_random"}}, {"round", {1, "b_round"}}, {"floor", {1, "b_floor"}},
        {"ceil", {1, "b_ceil"}},     {"abs", {1, "b_abs"}},     {"sqrt", {1, "b_sqrt"}},
        {"min", {2, "b_min"}},       {"max", {2, "b_max"}},     {"num", {1, "b_num"}},
        {"str", {1, "b_str"}},       {"len", {1, "b_len"}},     {"push", {2, "b_push"}},
        {"pop", {1, "b_pop"}},       {"sort", {1, "b_sort"}},
        {"split", {2, "b_split"}},   {"join", {2, "b_join"}},
        {"upper", {1, "b_upper"}},   {"lower", {1, "b_lower"}},
        {"find", {2, "b_find"}},     {"replace", {3, "b_replace"}},
        {"substr", {3, "b_substr"}}, {"readfile", {1, "b_readfile"}},
        {"writefile", {2, "b_writefile"}}, {"time", {0, "b_time"}},
        {"exists", {1, "b_exists"}}, {"appendfile", {2, "b_appendfile"}},
        {"exit", {0, "b_exit"}},     {"error", {1, "b_error"}},
        {"copy", {1, "b_copy"}},
        {"keys", {1, "b_keys"}},     {"has", {2, "b_has"}},
        {"remove", {2, "b_remove"}}, {"reverse", {1, "b_reverse"}},
    };
    // 인자를 하나 더 받을 수 있는 내장 함수: 이름 → 최대 인자 수
    std::map<string, int> builtinMaxArgs = { {"round", 2} };

    static LangError err(int line, const string& m) {
        return LangError(lineTag(line) + "" + m);
    }

    // 사용자 이름 → 안전한 C++ 식별자 (한글 등 non-ASCII 는 _XX 헥스로)
    static string mangle(const string& n, const char* prefix) {
        string o = prefix;
        for (unsigned char c : n) {
            if (isalnum(c) || c == '_') o += (char)c;
            else { char b[4]; snprintf(b, sizeof b, "%02X", c); o += '_'; o += b; }
        }
        return o;
    }
    static string varName (const string& n) { return mangle(n, "u_"); }
    static string funcName(const string& n) { return mangle(n, "f_"); }

    bool declared(const string& n) {
        return (inFunc && localSet.count(n)) || globalSet.count(n);
    }
    // 오타 제안 후보 — 지금 보이는 변수 이름들 / 부를 수 있는 이름들
    std::vector<string> visibleVars() {
        std::vector<string> out(globalSet.begin(), globalSet.end());
        if (inFunc) out.insert(out.end(), localSet.begin(), localSet.end());
        return out;
    }
    std::vector<string> callableNames() {
        std::vector<string> out = BUILTIN_NAMES;
        for (auto& [k, v] : funcs)   out.push_back(k);
        for (auto& [k, v] : classes) out.push_back(k);
        return out;
    }

    // 문자열 리터럴 → C++ 소스용 이스케이프
    static string cppStr(const string& s) {
        string o = "\"";
        for (char c : s) {
            switch (c) {
                case '"':  o += "\\\""; break;
                case '\\': o += "\\\\"; break;
                case '\n': o += "\\n";  break;
                case '\t': o += "\\t";  break;
                case '\r': o += "\\r";  break;
                default:   o += c;
            }
        }
        return o + "\"";
    }

    // let / for 로 만들어지는 변수 이름 수집 (함수 안은 별도라 제외)
    void collectVars(Stmt* s, std::set<string>& out) {
        if (auto* l = dynamic_cast<LetStmt*>(s))   { out.insert(l->name); return; }
        if (auto* f = dynamic_cast<ForStmt*>(s))   { out.insert(f->var); collectVars(f->body.get(), out); return; }
        if (auto* fe = dynamic_cast<ForEachStmt*>(s)) { out.insert(fe->var); collectVars(fe->body.get(), out); return; }
        if (auto* b = dynamic_cast<BlockStmt*>(s)) { for (auto& c : b->stmts) collectVars(c.get(), out); return; }
        if (auto* i = dynamic_cast<IfStmt*>(s)) {
            collectVars(i->thenB.get(), out);
            if (i->elseB) collectVars(i->elseB.get(), out);
            return;
        }
        if (auto* w = dynamic_cast<WhileStmt*>(s)) { collectVars(w->body.get(), out); return; }
        if (auto* t = dynamic_cast<TryStmt*>(s)) {
            out.insert(t->var);
            collectVars(t->tryB.get(), out);
            collectVars(t->catchB.get(), out);
            return;
        }
        // FuncStmt 내부는 그 함수의 지역이므로 여기서 수집하지 않음
    }
    // 함수 정의 수집 (중첩 포함 — 전부 최상위 C++ 함수로 끌어올림)
    void collectFuncs(Stmt* s) {
        if (auto* c = dynamic_cast<ClassStmt*>(s)) {
            if (builtins.count(c->name))
                throw LangError("클래스 이름 '" + c->name + "' 은 내장 함수와 겹칩니다");
            classes[c->name] = c;
            for (auto& m : c->methodList)
                collectFuncs(m->body.get());   // 메서드 안의 중첩 func 만 끌어올림
            return;
        }
        if (auto* f = dynamic_cast<FuncStmt*>(s)) {
            if (builtins.count(f->name))
                throw LangError("함수 이름 '" + f->name + "' 은 내장 함수와 겹칩니다");
            funcs[f->name] = f;
            collectFuncs(f->body.get());
            return;
        }
        if (auto* b = dynamic_cast<BlockStmt*>(s)) { for (auto& c : b->stmts) collectFuncs(c.get()); return; }
        if (auto* i = dynamic_cast<IfStmt*>(s)) {
            collectFuncs(i->thenB.get());
            if (i->elseB) collectFuncs(i->elseB.get());
            return;
        }
        if (auto* w = dynamic_cast<WhileStmt*>(s)) { collectFuncs(w->body.get()); return; }
        if (auto* f = dynamic_cast<ForStmt*>(s))   { collectFuncs(f->body.get()); return; }
        if (auto* fe = dynamic_cast<ForEachStmt*>(s)) { collectFuncs(fe->body.get()); return; }
        if (auto* t = dynamic_cast<TryStmt*>(s)) {
            collectFuncs(t->tryB.get());
            collectFuncs(t->catchB.get());
            return;
        }
    }

    // ---- 표현식 → C++ 식 문자열 ----
    string genExpr(Expr* e) {
        if (auto* n = dynamic_cast<NumExpr*>(e)) {
            char buf[64];
            if (std::fabs(n->v) < 9.0e18 && n->v == (long long)n->v)
                snprintf(buf, sizeof buf, "Value(%lld.0)", (long long)n->v);
            else
                snprintf(buf, sizeof buf, "Value(%.17g)", n->v);   // 1e+06 같은 표기도 유효한 리터럴
            return string(buf);
        }
        if (auto* s = dynamic_cast<StrExpr*>(e))
            return "Value(string(" + cppStr(s->s) + "))";
        if (auto* v = dynamic_cast<VarExpr*>(e)) {
            if (!declared(v->name))
                throw err(v->line, "정의되지 않은 변수: " + v->name + suggestName(v->name, visibleVars()));
            return varName(v->name);
        }
        if (auto* l = dynamic_cast<ListExpr*>(e)) {
            string o = "mk_list({";
            for (size_t i = 0; i < l->items.size(); i++) {
                if (i) o += ", ";
                o += genExpr(l->items[i].get());
            }
            return o + "})";
        }
        if (auto* m = dynamic_cast<MapExpr*>(e)) {
            string o = "mk_map({";
            for (size_t i = 0; i < m->items.size(); i++) {
                if (i) o += ", ";
                o += "{" + genExpr(m->items[i].first.get()) + ", "
                   + genExpr(m->items[i].second.get()) + "}";
            }
            return o + "})";
        }
        if (auto* ix = dynamic_cast<IndexExpr*>(e))
            return "idx_get(" + genExpr(ix->target.get()) + ", " + genExpr(ix->index.get()) + ")";
        if (auto* f = dynamic_cast<FieldExpr*>(e))
            return "fld_get(" + genExpr(f->target.get()) + ", " + cppStr(f->field) + ")";
        if (auto* mc = dynamic_cast<MethodCallExpr*>(e)) {
            // 어느 클래스에도 없는 메서드면 오타다. 인터프리터는 그 줄이 돌 때
            // 오타 제안까지 붙여 알려 주는데, 빌드본은 클래스를 런타임에나 알아
            // 제안을 못 한다 — 그래서 여기서, 빌드 시점에 같은 수준으로 잡는다
            // (변수·필드를 빌드 시점에 잡는 것과 같은 방식).
            {
                bool known = false;
                std::vector<string> names;
                for (auto& [cn, cls] : classes) {
                    (void)cn;
                    for (auto& [mn, fn] : cls->methods) { (void)fn; names.push_back(mn); }
                    if (cls->methods.count(mc->method)) known = true;
                }
                if (!known) {
                    // upper/append 처럼 "파이썬이었으면 메서드였을" 이름이면 오타 제안보다
                    // 쓰는 법을 알려 주는 쪽이 낫다 (인터프리터와 같은 문구).
                    string hint = methodHint(mc->method);
                    throw err(mc->line, "메서드 '" + mc->method + "'"
                              + josa(mc->method, "이", "가") + " 어느 클래스에도 없습니다"
                              + (hint.empty() ? suggestName(mc->method, names) : hint));
                }
            }
            methodCalls.insert({mc->method, (int)mc->args.size()});
            string o = "d_" + mangle(mc->method, "") + "_" + std::to_string(mc->args.size())
                     + "(" + genExpr(mc->target.get());
            for (auto& a : mc->args) o += ", " + genExpr(a.get());
            return o + ")";
        }
        if (auto* b = dynamic_cast<BinExpr*>(e)) {
            string L = genExpr(b->lhs.get()), R = genExpr(b->rhs.get());
            switch (b->op) {
                case Tok::PLUS:    return "vadd(" + L + ", " + R + ")";
                case Tok::MINUS:   return "vsub(" + L + ", " + R + ")";
                case Tok::STAR:    return "vmul(" + L + ", " + R + ")";
                case Tok::SLASH:   return "vdiv(" + L + ", " + R + ")";
                case Tok::PERCENT: return "vmod(" + L + ", " + R + ")";
                case Tok::EQ:      return "c_eq(" + L + ", " + R + ")";
                case Tok::NEQ:     return "c_ne(" + L + ", " + R + ")";
                case Tok::LT:      return "c_lt(" + L + ", " + R + ")";
                case Tok::GT:      return "c_gt(" + L + ", " + R + ")";
                case Tok::LE:      return "c_le(" + L + ", " + R + ")";
                case Tok::GE:      return "c_ge(" + L + ", " + R + ")";
                default: throw err(b->line, "지원하지 않는 연산자");
            }
        }
        if (auto* n = dynamic_cast<NegExpr*>(e))  return "vneg(" + genExpr(n->inner.get()) + ")";
        if (auto* n = dynamic_cast<NotExpr*>(e))  return "Value(truthy(" + genExpr(n->inner.get()) + ") ? 0.0 : 1.0)";
        if (auto* in = dynamic_cast<InputExpr*>(e)) return "my_input(" + cppStr(in->prompt) + ")";
        if (auto* lg = dynamic_cast<LogicalExpr*>(e)) {
            // C++ 의 &&/|| 가 단락 평가를 해주므로 그대로 이용
            string L = "truthy(" + genExpr(lg->lhs.get()) + ")";
            string R = "truthy(" + genExpr(lg->rhs.get()) + ")";
            string op = (lg->op == Tok::AND) ? " && " : " || ";
            return "Value((" + L + op + R + ") ? 1.0 : 0.0)";
        }
        if (auto* c = dynamic_cast<CallExpr*>(e)) {
            string argsCode;
            for (size_t i = 0; i < c->args.size(); i++) {
                if (i) argsCode += ", ";
                argsCode += genExpr(c->args[i].get());
            }
            auto bi = builtins.find(c->name);
            if (bi != builtins.end()) {
                int lo = bi->second.first, n = (int)c->args.size();
                auto mx = builtinMaxArgs.find(c->name);
                int hi = (mx != builtinMaxArgs.end()) ? mx->second : lo;
                if (n < lo || n > hi)
                    throw err(c->line, c->name + "() 는 인자 "
                              + (lo == hi ? std::to_string(lo) : std::to_string(lo) + "~" + std::to_string(hi))
                              + "개가 필요합니다 (지금 " + std::to_string(n) + "개)");
                return bi->second.second + "(" + argsCode + ")";
            }
            auto cc = classes.find(c->name);
            if (cc != classes.end()) {   // 클래스 생성자
                auto initIt = cc->second->methods.find("init");
                size_t need = (initIt != cc->second->methods.end()) ? initIt->second->params.size() : 0;
                if (c->args.size() != need)
                    throw err(c->line, c->name + "() 생성자는 인자 " + std::to_string(need)
                              + "개가 필요합니다 (지금 " + std::to_string(c->args.size()) + "개)");
                return "new_" + mangle(c->name, "") + "(" + argsCode + ")";
            }
            auto uf = funcs.find(c->name);
            if (uf == funcs.end())
                throw err(c->line, "정의되지 않은 함수 또는 클래스: " + c->name
                                   + suggestName(c->name, callableNames()));
            if (c->args.size() != uf->second->params.size())
                throw err(c->line, c->name + "() 는 인자 " + std::to_string(uf->second->params.size())
                          + "개가 필요합니다 (지금 " + std::to_string(c->args.size()) + "개)");
            return funcName(c->name) + "(" + argsCode + ")";
        }
        throw LangError("내부 오류: 변환할 수 없는 표현식");
    }

    // ---- 문장 → C++ 코드 (out 에 누적) ----
    void ind(std::ostringstream& out, int depth) { for (int i = 0; i < depth; i++) out << "    "; }

    void genStmt(Stmt* s, std::ostringstream& out, int depth) {
        if (auto* l = dynamic_cast<LetStmt*>(s)) {
            ind(out, depth);
            out << varName(l->name) << " = " << genExpr(l->val.get()) << ";\n";
            return;
        }
        if (auto* a = dynamic_cast<AssignStmt*>(s)) {
            if (!declared(a->name)) {
                string hint = suggestName(a->name, visibleVars());
                if (hint.empty()) hint = "  (" + KW_LET + " " + a->name + " = ... 로 먼저 선언하세요)";
                throw err(a->line, "선언되지 않은 변수에 대입: " + a->name + hint);
            }
            ind(out, depth);
            out << varName(a->name) << " = " << genExpr(a->val.get()) << ";\n";
            return;
        }
        if (auto* pa = dynamic_cast<PathAssignStmt*>(s)) {
            if (!declared(pa->name))
                throw err(pa->line, "정의되지 않은 변수: " + pa->name + suggestName(pa->name, visibleVars()));
            string target = varName(pa->name);
            for (size_t k = 0; k + 1 < pa->path.size(); k++) {
                Accessor& a = pa->path[k];
                target = a.isField
                    ? "fld_mid(" + target + ", " + cppStr(a.field) + ")"
                    : "idx_mid(" + target + ", " + genExpr(a.index.get()) + ")";
            }
            Accessor& last = pa->path.back();
            string slot = last.isField
                ? "fld_put(" + target + ", " + cppStr(last.field) + ")"
                : "idx_put(" + target + ", " + genExpr(last.index.get()) + ")";
            ind(out, depth);
            out << slot << " = " << genExpr(pa->val.get()) << ";\n";
            return;
        }
        if (auto* pc = dynamic_cast<PathCompoundStmt*>(s)) {
            if (!declared(pc->name))
                throw err(pc->line, "정의되지 않은 변수: " + pc->name + suggestName(pc->name, visibleVars()));
            string target = varName(pc->name);
            for (auto& a : pc->path)
                target = a.isField
                    ? "fld_mid(" + target + ", " + cppStr(a.field) + ")"
                    : "idx_mid(" + target + ", " + genExpr(a.index.get()) + ")";
            const char* fn = pc->op == Tok::PLUS  ? "vadd"
                           : pc->op == Tok::MINUS ? "vsub"
                           : pc->op == Tok::STAR  ? "vmul" : "vdiv";
            string EL = "__el" + std::to_string(tmpN++);
            ind(out, depth);
            out << "{ Value& " << EL << " = " << target << "; "
                << EL << " = " << fn << "(" << EL << ", " << genExpr(pc->rhs.get()) << "); }\n";
            return;
        }
        if (auto* p = dynamic_cast<PrintStmt*>(s)) {
            ind(out, depth);
            out << "my_print({";
            for (size_t i = 0; i < p->vals.size(); i++) {
                if (i) out << ", ";
                // 중괄호 초기화 리스트는 왼쪽부터 순서대로 평가되므로,
                // 인자마다 즉시 toString() 하면 인터프리터와 시점이 같아짐
                out << "(" << genExpr(p->vals[i].get()) << ").toString()";
            }
            out << "});\n";
            return;
        }
        if (auto* es = dynamic_cast<ExprStmt*>(s)) {
            ind(out, depth);
            out << "(void)(" << genExpr(es->e.get()) << ");\n";
            return;
        }
        if (auto* b = dynamic_cast<BlockStmt*>(s)) {
            for (auto& c : b->stmts) genStmt(c.get(), out, depth);
            return;
        }
        if (auto* i = dynamic_cast<IfStmt*>(s)) {
            ind(out, depth);
            out << "if (truthy(" << genExpr(i->cond.get()) << ")) {\n";
            genStmt(i->thenB.get(), out, depth + 1);
            ind(out, depth); out << "}\n";
            if (i->elseB) {
                if (auto* chain = dynamic_cast<IfStmt*>(i->elseB.get())) {
                    ind(out, depth); out << "else\n";
                    genStmt(chain, out, depth);           // else if 체인
                } else {
                    ind(out, depth); out << "else {\n";
                    genStmt(i->elseB.get(), out, depth + 1);
                    ind(out, depth); out << "}\n";
                }
            }
            return;
        }
        if (auto* w = dynamic_cast<WhileStmt*>(s)) {
            ind(out, depth);
            out << "while (truthy(" << genExpr(w->cond.get()) << ")) {\n";
            loopDepth++;
            genStmt(w->body.get(), out, depth + 1);
            loopDepth--;
            ind(out, depth); out << "}\n";
            return;
        }
        if (auto* f = dynamic_cast<ForStmt*>(s)) {
            int id = tmpN++;
            string S = "__s" + std::to_string(id), E = "__e" + std::to_string(id),
                   T = "__t" + std::to_string(id), I = "__i" + std::to_string(id);
            ind(out, depth); out << "{\n";
            ind(out, depth + 1);
            out << "double " << S << " = needNum(" << genExpr(f->start.get()) << ", \"" << KW_FOR << "\");\n";
            ind(out, depth + 1);
            out << "double " << E << " = needNum(" << genExpr(f->end.get()) << ", \"" << KW_FOR << "\");\n";
            ind(out, depth + 1);
            if (f->step) {
                out << "double " << T << " = needNum(" << genExpr(f->step.get()) << ", \"" << KW_STEP << "\");\n";
                ind(out, depth + 1);
                out << "if (" << T << " == 0) throw RunErr(\"" << KW_STEP << " 은 0이 아닌 숫자여야 합니다\");\n";
            } else {
                out << "double " << T << " = (" << S << " <= " << E << ") ? 1.0 : -1.0;\n";
            }
            ind(out, depth + 1);
            out << "for (double " << I << " = " << S << "; " << T << " > 0 ? " << I << " <= " << E
                << " : " << I << " >= " << E << "; " << I << " += " << T << ") {\n";
            ind(out, depth + 2);
            out << varName(f->var) << " = Value(" << I << ");\n";
            loopDepth++;
            genStmt(f->body.get(), out, depth + 2);
            loopDepth--;
            ind(out, depth + 1); out << "}\n";
            ind(out, depth); out << "}\n";
            return;
        }
        if (auto* fe = dynamic_cast<ForEachStmt*>(s)) {
            int id = tmpN++;
            string IT = "__items" + std::to_string(id), EL = "__e" + std::to_string(id);
            ind(out, depth); out << "{\n";
            ind(out, depth + 1);
            out << "auto " << IT << " = iter_items(" << genExpr(fe->iter.get()) << ");\n";
            ind(out, depth + 1);
            out << "for (auto& " << EL << " : " << IT << ") {\n";
            ind(out, depth + 2);
            out << varName(fe->var) << " = " << EL << ";\n";
            loopDepth++;
            genStmt(fe->body.get(), out, depth + 2);
            loopDepth--;
            ind(out, depth + 1); out << "}\n";
            ind(out, depth); out << "}\n";
            return;
        }
        if (auto* t = dynamic_cast<TryStmt*>(s)) {
            int id = tmpN++;
            ind(out, depth); out << "try {\n";
            genStmt(t->tryB.get(), out, depth + 1);
            ind(out, depth); out << "} catch (RunErr& __err" << id << ") {\n";
            ind(out, depth + 1);
            out << varName(t->var) << " = Value(string(__err" << id << ".what()));\n";
            genStmt(t->catchB.get(), out, depth + 1);
            ind(out, depth); out << "}\n";
            return;
        }
        if (dynamic_cast<BreakStmt*>(s)) {
            if (loopDepth == 0) throw LangError(KW_BREAK + " 는 반복문 안에서만 쓸 수 있습니다");
            ind(out, depth); out << "break;\n";
            return;
        }
        if (dynamic_cast<ContinueStmt*>(s)) {
            if (loopDepth == 0) throw LangError(KW_CONTINUE + " 는 반복문 안에서만 쓸 수 있습니다");
            ind(out, depth); out << "continue;\n";
            return;
        }
        if (auto* r = dynamic_cast<ReturnStmt*>(s)) {
            if (!inFunc) throw LangError(KW_RETURN + " 은 함수 안에서만 쓸 수 있습니다");
            ind(out, depth);
            out << "return " << (r->val ? genExpr(r->val.get()) : string("Value(0.0)")) << ";\n";
            return;
        }
        if (dynamic_cast<FuncStmt*>(s)) return;   // 함수 정의는 별도로 방출
        if (dynamic_cast<ClassStmt*>(s)) return;  // 클래스도 별도로 방출
        throw LangError("내부 오류: 변환할 수 없는 문장");
    }

    // ---- 전체 프로그램 → 완성된 C++ 소스 ----
    string generate(std::vector<StmtP>& program) {
        for (auto& s : program) collectFuncs(s.get());
        for (auto& s : program) collectVars(s.get(), globalSet);

        // ---- 본문들을 먼저 생성 (메서드 호출 사용 기록 수집을 위해) ----
        auto emitCallable = [&](const string& cppName, FuncStmt* fn, bool withSelf) {
            inFunc = true;
            localSet.clear();
            if (withSelf) localSet.insert("self");
            for (auto& p : fn->params) localSet.insert(p);
            std::set<string> bodyVars;
            collectVars(fn->body.get(), bodyVars);
            std::ostringstream fb;
            fb << "static Value " << cppName << "(";
            bool first = true;
            if (withSelf) { fb << "Value " << varName("self"); first = false; }
            for (auto& p : fn->params) {
                if (!first) fb << ", ";
                first = false;
                fb << "Value " << varName(p);
            }
            fb << ") {\n    DG __depth_guard;\n";
            for (auto& v : bodyVars)
                if (!localSet.count(v)) {
                    fb << "    Value " << varName(v) << "{};\n";
                    localSet.insert(v);
                }
            genStmt(fn->body.get(), fb, 1);
            fb << "    return Value(0.0);\n}\n\n";
            inFunc = false;
            localSet.clear();
            return fb.str();
        };
        auto sig = [&](const string& cppName, FuncStmt* fn, bool withSelf) {
            string o = "static Value " + cppName + "(";
            bool first = true;
            if (withSelf) { o += "Value " + varName("self"); first = false; }
            for (auto& p : fn->params) {
                if (!first) o += ", ";
                first = false;
                o += "Value " + varName(p);
            }
            return o + ");\n";
        };
        auto methodCpp = [&](const string& cls, const string& m) {
            return "m_" + mangle(cls, "") + "_" + mangle(m, "");
        };

        std::ostringstream funcDefs, methodDefs, ctorDefs;
        for (auto& [name, fn] : funcs)
            funcDefs << emitCallable(funcName(name), fn, false);
        for (auto& [cname, cls] : classes)
            for (auto& m : cls->methodList)
                methodDefs << emitCallable(methodCpp(cname, m->name), m.get(), true);
        for (auto& [cname, cls] : classes) {
            auto initIt = cls->methods.find("init");
            ctorDefs << "static Value new_" << mangle(cname, "") << "(";
            if (initIt != cls->methods.end())
                for (size_t i = 0; i < initIt->second->params.size(); i++) {
                    if (i) ctorDefs << ", ";
                    ctorDefs << "Value __a" << i;
                }
            ctorDefs << ") {\n"
                     << "    Value __o; __o.kind = Value::OBJ; __o.className = "
                     << cppStr(cname) << "; __o.map = std::make_shared<Map>();\n";
            if (initIt != cls->methods.end()) {
                ctorDefs << "    " << methodCpp(cname, "init") << "(__o";
                for (size_t i = 0; i < initIt->second->params.size(); i++)
                    ctorDefs << ", __a" << i;
                ctorDefs << ");\n";
            }
            ctorDefs << "    return __o;\n}\n\n";
        }

        // main 본문 (여기서도 methodCalls 가 채워짐)
        std::ostringstream mainBody;
        for (auto& s : program) genStmt(s.get(), mainBody, 2);

        // 디스패처: 같은 이름/인자수 메서드 호출을 클래스별 함수로 분기
        std::ostringstream dispDefs, dispDecls;
        for (auto& [mname, argc] : methodCalls) {
            string dn = "d_" + mangle(mname, "") + "_" + std::to_string(argc);
            string params = "Value __self";
            string passArgs;
            for (int i = 0; i < argc; i++) {
                params += ", Value __a" + std::to_string(i);
                passArgs += ", __a" + std::to_string(i);
            }
            dispDecls << "static Value " << dn << "(" << params << ");\n";
            dispDefs << "static Value " << dn << "(" << params << ") {\n"
                     << "    if (__self.kind != Value::OBJ)\n"
                     << "        throw RunErr(__self.kindName() + "
                     << cppStr("에는 메서드를 호출할 수 없습니다" + methodHint(mname)) << ");\n";
            for (auto& [cname, cls] : classes) {
                auto mit = cls->methods.find(mname);
                if (mit == cls->methods.end()) continue;
                dispDefs << "    if (__self.className == " << cppStr(cname) << ") {\n";
                if ((int)mit->second->params.size() == argc)
                    dispDefs << "        return " << methodCpp(cname, mname) << "(__self" << passArgs << ");\n";
                else
                    dispDefs << "        throw RunErr(\"" << mname << "() 는 인자 "
                             << mit->second->params.size() << "개가 필요합니다 (지금 " << argc << "개)\");\n";
                dispDefs << "    }\n";
            }
            dispDefs << "    throw RunErr(\"클래스 '\" + __self.className + \"' 에 메서드 '"
                     << mname << "'" << josa(mname, "이", "가") << " 없습니다\");\n}\n\n";
        }

        // ---- 최종 조립 ----
        std::ostringstream out;
        out << RUNTIME << "\n";
        for (auto& [name, fn] : funcs) out << sig(funcName(name), fn, false);
        for (auto& [cname, cls] : classes)
            for (auto& m : cls->methodList)
                out << sig(methodCpp(cname, m->name), m.get(), true);
        for (auto& [cname, cls] : classes) {
            auto initIt = cls->methods.find("init");
            out << "static Value new_" << mangle(cname, "") << "(";
            if (initIt != cls->methods.end())
                for (size_t i = 0; i < initIt->second->params.size(); i++) {
                    if (i) out << ", ";
                    out << "Value";
                }
            out << ");\n";
        }
        out << dispDecls.str();
        for (auto& g : globalSet)
            out << "static Value " << varName(g) << "{};\n";
        out << "\n";
        out << funcDefs.str() << methodDefs.str() << ctorDefs.str() << dispDefs.str();

        // main
        out << "int main() {\n"
               "#ifdef _WIN32\n"
               "    SetConsoleCP(CP_UTF8);\n"
               "    SetConsoleOutputCP(CP_UTF8);\n"
               "#endif\n"
               "    try {\n";
        out << mainBody.str();
        out << "    } catch (ExitSig&) {\n"
               "        return 0;\n"
               "    } catch (const RunErr& e) {\n"
               "        std::cout << \"!! 에러: \" << e.what() << \"\\n\";\n"
               "        return 1;\n"
               "    }\n"
               "    return 0;\n"
               "}\n";
        return out.str();
    }
};

// ============================================================
//  파이썬 변환기 (topython) — "다음 언어로 나가는 길"
//  C++ 백엔드(CodeGen)와 목표가 정반대다: 맹글링도 런타임 라이브러리도 없이
//  **사람이 읽는 파이썬**을 낸다. 학생이 자기 프로그램을 알아볼 수 있어야 한다.
//  파이썬 3 식별자는 한글을 그대로 받으므로 변수/함수 이름이 살아남는다.
//  Venos 와 파이썬이 다른 지점은 숨기지 않고 파일 머리말에 적어 둔다.
// ============================================================
struct PyGen {
    std::set<string> imports;                // math, random, time, os, sys, copy
    std::set<string> helpers;                // 실제로 쓴 도우미만 앞에 붙인다
    std::map<string, FuncStmt*> funcs;
    std::map<string, ClassStmt*> classes;
    std::set<string> globalSet;              // 최상위에서 만들어지는 변수
    std::set<string> localSet;               // 현재 함수의 지역 (인자 + let)
    std::set<string> touchedGlobals;         // 현재 함수가 대입한 전역 → global 선언
    std::set<string> intVars;                // for i = a to b 로 묶인 변수 (range 라 항상 정수)
    std::set<string> strVars;                // 대입이 전부 문자열인 변수 (inferStrVars 가 채운다)
    bool inFunc = false;
    bool sawMap = false;                     // 딕셔너리가 존재할 수 있는가 (1차 통과에서 알아낸다)
    bool sawList = false;                    // 리스트가 존재할 수 있는가
    bool sawIndex = false;                   // [ ] 인덱싱을 쓰는가 (머리말에 1부터 얘기를 넣을지)
    bool sawFloat = false;                   // 소수가 나올 수 있는가 (/ · sqrt · 입력 등)
    bool sawCase  = false;                   // upper()/lower() 를 썼는가 (머리말 주의 문구용)
    bool sawNonAscii = false;                // 문자열 리터럴에 ASCII 밖 글자가 있는가 (출력 인코딩)
    bool sawInput = false;                   // input 을 쓰는가 (입력 인코딩)
    bool lastRangeIsInt = false;             // 방금 만든 for 범위가 진짜 range() 인가 (rangeOf 가 설정)


    static LangError err(int line, const string& m) {
        return LangError(lineTag(line) + m);
    }
    static LangError nope(int line, const string& what) {
        return LangError(lineTag(line) + "파이썬으로 변환할 수 없습니다: " + what);
    }

    // ---- 이름 ----
    // 파이썬 예약어와, 우리가 앞에 붙이는 도우미/모듈 이름을 피한다.
    static bool taken(const string& n) {
        static const std::set<string> R = {
            "False","None","True","and","as","assert","async","await","break","class",
            "continue","def","del","elif","else","except","finally","for","from","global",
            "if","import","in","is","lambda","nonlocal","not","or","pass","raise","return",
            "try","while","with","yield",
            "math","random","time","os","sys","copy",   // 우리가 import 하는 모듈
            "print","input","len","abs","min","max","range","sorted","int","float","str",
        };
        return R.count(n) > 0 || (n.size() > 1 && n[0] == '_');
    }
    static string pyName(const string& n) { return taken(n) ? n + "_" : n; }

    // 문자열 리터럴 → 파이썬 소스
    string pyStrTracked(const string& s) {
        for (unsigned char c : s) if (c > 0x7F) { sawNonAscii = true; break; }
        return pyStr(s);
    }
    static string pyStr(const string& s) {
        string o = "\"";
        for (char c : s) {
            switch (c) {
                case '"':  o += "\\\""; break;
                case '\\': o += "\\\\"; break;
                case '\n': o += "\\n";  break;
                case '\t': o += "\\t";  break;
                case '\r': o += "\\r";  break;
                default:   o += c;
            }
        }
        return o + "\"";
    }
    // 숫자 리터럴 — 정수는 정수로 낸다 (그래야 파이썬에서도 5 가 5 로 찍힌다)
    static string pyNum(double v) {
        char b[64];
        if (std::fabs(v) < 9.0e18 && v == (long long)v) { snprintf(b, sizeof b, "%lld", (long long)v); return b; }
        // 되돌려 읽어 같은 값이 되는 가장 짧은 표기 — 3.14159 를 3.1415899999999999 로 쓰지 않는다
        for (int prec = 15; prec <= 17; prec++) {
            snprintf(b, sizeof b, "%.*g", prec, v);
            if (std::strtod(b, nullptr) == v) break;
        }
        return b;
    }
    string need(const string& h) { helpers.insert(h); return "_" + h; }

    // ---- 우선순위 (Venos 와 파이썬이 같은 순서라 괄호를 최소로 낼 수 있다) ----
    enum { P_OR = 0, P_AND, P_NOT, P_CMP, P_ADD, P_MUL, P_UNARY, P_ATOM };
    // floor(a / b) 는 파이썬에서 a // b 로 낸다 (아래 floorDiv). 그러면 그 자리의
    // 우선순위가 원자가 아니라 곱셈이 되므로 여기서도 그렇게 답해야 한다 —
    // 안 그러면 2 * floor(x/2) 가 2 * x // 2 로 나가 (2*x)//2 가 된다.
    int precOf(Expr* e) {
        if (floorDiv(e)) return P_MUL;
        // not/and/or 는 int(...) 로 감싸서 내보내므로 밖에서 보면 원자다 (괄호가 필요 없다)
        if (dynamic_cast<LogicalExpr*>(e)) return P_ATOM;
        if (dynamic_cast<NotExpr*>(e))     return P_ATOM;
        if (auto* b = dynamic_cast<BinExpr*>(e)) {
            if (b->interpN > 0) return P_ATOM;          // f-string 은 원자
            switch (b->op) {
                case Tok::STAR: case Tok::SLASH: case Tok::PERCENT: return P_MUL;
                case Tok::PLUS: case Tok::MINUS:                    return P_ADD;
                default:                                            return P_CMP;
            }
        }
        if (dynamic_cast<NegExpr*>(e)) return P_UNARY;
        return P_ATOM;
    }
    string wrap(Expr* e, int parentPrec) {
        string s = expr(e);
        return precOf(e) < parentPrec ? "(" + s + ")" : s;
    }

    // ---- 조건 자리 ----
    // if/while 은 값이 1 인지 0 인지가 아니라 참/거짓만 본다. 그래서 not/and/or 를
    // int(...) 로 감쌀 이유가 없다 — while int(not q.비었나()) 가 아니라
    // while not q.비었나() 로 나가야 학생이 알아본다.
    // (Venos 와 파이썬의 참/거짓 판정은 0·""·빈 리스트·빈 딕셔너리에서 이미 같다)
    // 파이썬에서 이미 0/1 또는 bool 로 나오는 식인가 (bool() 을 덧씌울 필요가 없다)
    bool boolish(Expr* e) {
        if (dynamic_cast<NotExpr*>(e) || dynamic_cast<LogicalExpr*>(e)) return true;
        if (auto* b = dynamic_cast<BinExpr*>(e)) {
            if (b->interpN > 0) return false;
            switch (b->op) {
                case Tok::EQ: case Tok::NEQ: case Tok::LT:
                case Tok::GT: case Tok::LE:  case Tok::GE: return true;
                default: return false;
            }
        }
        if (auto* c = dynamic_cast<CallExpr*>(e))
            return c->name == "has" && c->args.size() == 2 && !funcs.count("has");
        return false;
    }
    int condPrec(Expr* e) {
        if (dynamic_cast<NotExpr*>(e)) return P_NOT;
        if (auto* lg = dynamic_cast<LogicalExpr*>(e)) return lg->op == Tok::AND ? P_AND : P_OR;
        return precOf(e);
    }
    string condWrap(Expr* e, int parentPrec) {
        string s = cond(e);
        return condPrec(e) < parentPrec ? "(" + s + ")" : s;
    }
    string cond(Expr* e) {
        if (auto* n = dynamic_cast<NotExpr*>(e))
            return "not " + condWrap(n->inner.get(), P_NOT + 1);
        if (auto* lg = dynamic_cast<LogicalExpr*>(e)) {
            int p = (lg->op == Tok::AND) ? P_AND : P_OR;
            return condWrap(lg->lhs.get(), p)
                 + ((lg->op == Tok::AND) ? " and " : " or ")
                 + condWrap(lg->rhs.get(), p + 1);
        }
        return expr(e);
    }

    // 문자열이 확실한 식인가 — Venos 의 "문자열 + 숫자" 자동 변환을 어디서 흉내낼지,
    // 그리고 파이썬에서 s * n / s.find(x) 를 그대로 써도 되는지 판단한다.
    bool stringish(Expr* e) {
        if (dynamic_cast<StrExpr*>(e)) return true;
        if (auto* v = dynamic_cast<VarExpr*>(e)) return strVars.count(v->name) > 0;
        if (auto* b = dynamic_cast<BinExpr*>(e)) {
            if (b->interpN > 0) return true;
            if (b->op == Tok::PLUS)  return stringish(b->lhs.get()) || stringish(b->rhs.get());
            if (b->op == Tok::STAR)  return stringish(b->lhs.get()) || stringish(b->rhs.get());
            return false;
        }
        if (auto* c = dynamic_cast<CallExpr*>(e)) {
            static const std::set<string> S = {"str","upper","lower","join","replace","substr","readfile"};
            if (c->name == "reverse" && c->args.size() == 1) return stringish(c->args[0].get());
            return S.count(c->name) > 0 && !funcs.count(c->name);
        }
        return false;
    }

    // ---- 어떤 변수가 확실히 문자열인가 (최대 고정점) ----
    // 낙관적으로 전부 후보로 두고, 문자열이 아닌 대입이 하나라도 있으면 뺀다.
    // 그래야 결과 = 결과 + 글자 처럼 자기 자신을 쓰는 누적도 문자열로 인정된다.
    // 함수 인자·for 범위 변수처럼 값을 알 수 없는 이름은 아예 후보에서 뺀다 (안전한 쪽).
    std::map<string, std::vector<Expr*>> strSites;   // nullptr = 무조건 문자열인 자리
    std::set<string> strBanned;
    void scanStr(Stmt* s) {
        if (!s) return;
        if (auto* l = dynamic_cast<LetStmt*>(s)) {
            if (l->val) strSites[l->name].push_back(l->val.get());
            else        strBanned.insert(l->name);          // let x  → 0
            return;
        }
        if (auto* a = dynamic_cast<AssignStmt*>(s)) { strSites[a->name].push_back(a->val.get()); return; }
        if (auto* f = dynamic_cast<ForStmt*>(s))   { strBanned.insert(f->var); scanStr(f->body.get()); return; }
        if (auto* fe = dynamic_cast<ForEachStmt*>(s)) {
            if (stringishLiteral(fe->iter.get())) strSites[fe->var].push_back(nullptr);
            else                                  strBanned.insert(fe->var);
            scanStr(fe->body.get());
            return;
        }
        if (auto* t = dynamic_cast<TryStmt*>(s)) {
            strSites[t->var].push_back(nullptr);            // catch 변수는 항상 에러 메시지(문자열)
            scanStr(t->tryB.get()); scanStr(t->catchB.get());
            return;
        }
        if (auto* b = dynamic_cast<BlockStmt*>(s)) { for (auto& c : b->stmts) scanStr(c.get()); return; }
        if (auto* i = dynamic_cast<IfStmt*>(s))    { scanStr(i->thenB.get()); scanStr(i->elseB.get()); return; }
        if (auto* w = dynamic_cast<WhileStmt*>(s)) { scanStr(w->body.get()); return; }
        if (auto* fn = dynamic_cast<FuncStmt*>(s)) {
            for (auto& p : fn->params) strBanned.insert(p);
            scanStr(fn->body.get());
            return;
        }
        if (auto* cl = dynamic_cast<ClassStmt*>(s)) {
            for (auto& m : cl->methodList) {
                for (auto& p : m->params) strBanned.insert(p);
                scanStr(m->body.get());
            }
            return;
        }
    }
    // for ... in 의 대상이 글자 단위로 도는 문자열인가 (strVars 를 아직 모르는 단계라 리터럴만 본다)
    static bool stringishLiteral(Expr* e) {
        if (dynamic_cast<StrExpr*>(e)) return true;
        if (auto* b = dynamic_cast<BinExpr*>(e)) return b->interpN > 0;
        // 문자열만 들어 있는 리스트 리터럴을 돌면 그 변수도 문자열이다
        if (auto* l = dynamic_cast<ListExpr*>(e)) {
            if (l->items.empty()) return false;
            for (auto& it : l->items) if (!stringishLiteral(it.get())) return false;
            return true;
        }
        return false;
    }
    // ---- 재귀가 있는가 ----
    // Venos 는 재귀를 2000번까지 허용하고 파이썬 기본값은 1000이다. 그대로 두면 Venos 에선
    // 잘 돌던 재귀가 파이썬에서 RecursionError 로 죽는다 — "같은 프로그램"이 아니게 된다.
    // 호출 그래프에 회로가 있으면(자기 자신 호출 포함) 파이썬 쪽 한도를 올려 준다.
    std::map<string, std::set<string>> callGraph;
    void callsIn(Expr* e, std::set<string>& out) {
        if (!e) return;
        if (auto* c = dynamic_cast<CallExpr*>(e)) {
            if (funcs.count(c->name)) out.insert(c->name);
            for (auto& a : c->args) callsIn(a.get(), out);
            return;
        }
        if (auto* m = dynamic_cast<MethodCallExpr*>(e)) {
            out.insert("." + m->method);          // 메서드는 이름만 보고 잇는다 (동적 호출이라)
            callsIn(m->target.get(), out);
            for (auto& a : m->args) callsIn(a.get(), out);
            return;
        }
        if (auto* b = dynamic_cast<BinExpr*>(e))   { callsIn(b->lhs.get(), out); callsIn(b->rhs.get(), out); return; }
        if (auto* l = dynamic_cast<LogicalExpr*>(e)){ callsIn(l->lhs.get(), out); callsIn(l->rhs.get(), out); return; }
        if (auto* n = dynamic_cast<NotExpr*>(e))   { callsIn(n->inner.get(), out); return; }
        if (auto* g = dynamic_cast<NegExpr*>(e))   { callsIn(g->inner.get(), out); return; }
        if (auto* i = dynamic_cast<IndexExpr*>(e)) { callsIn(i->target.get(), out); callsIn(i->index.get(), out); return; }
        if (auto* f = dynamic_cast<FieldExpr*>(e)) { callsIn(f->target.get(), out); return; }
        if (auto* li = dynamic_cast<ListExpr*>(e)) { for (auto& x : li->items) callsIn(x.get(), out); return; }
        if (auto* mp = dynamic_cast<MapExpr*>(e))  {
            for (auto& [k, v] : mp->items) { callsIn(k.get(), out); callsIn(v.get(), out); }
            return;
        }
    }
    void callsIn(Stmt* s, std::set<string>& out) {
        if (!s) return;
        if (auto* l  = dynamic_cast<LetStmt*>(s))          { callsIn(l->val.get(), out); return; }
        if (auto* a  = dynamic_cast<AssignStmt*>(s))       { callsIn(a->val.get(), out); return; }
        if (auto* pa = dynamic_cast<PathAssignStmt*>(s))   {
            for (auto& ac : pa->path) callsIn(ac.index.get(), out);
            callsIn(pa->val.get(), out); return;
        }
        if (auto* pc = dynamic_cast<PathCompoundStmt*>(s)) {
            for (auto& ac : pc->path) callsIn(ac.index.get(), out);
            callsIn(pc->rhs.get(), out); return;
        }
        if (auto* p  = dynamic_cast<PrintStmt*>(s))        { for (auto& v : p->vals) callsIn(v.get(), out); return; }
        if (auto* es = dynamic_cast<ExprStmt*>(s))         { callsIn(es->e.get(), out); return; }
        if (auto* b  = dynamic_cast<BlockStmt*>(s))        { for (auto& c : b->stmts) callsIn(c.get(), out); return; }
        if (auto* i  = dynamic_cast<IfStmt*>(s))           {
            callsIn(i->cond.get(), out); callsIn(i->thenB.get(), out); callsIn(i->elseB.get(), out); return;
        }
        if (auto* w  = dynamic_cast<WhileStmt*>(s))        { callsIn(w->cond.get(), out); callsIn(w->body.get(), out); return; }
        if (auto* f  = dynamic_cast<ForStmt*>(s))          {
            callsIn(f->start.get(), out); callsIn(f->end.get(), out); callsIn(f->step.get(), out);
            callsIn(f->body.get(), out); return;
        }
        if (auto* fe = dynamic_cast<ForEachStmt*>(s))      { callsIn(fe->iter.get(), out); callsIn(fe->body.get(), out); return; }
        if (auto* t  = dynamic_cast<TryStmt*>(s))          { callsIn(t->tryB.get(), out); callsIn(t->catchB.get(), out); return; }
        if (auto* r  = dynamic_cast<ReturnStmt*>(s))       { callsIn(r->val.get(), out); return; }
    }
    bool hasRecursion() {
        callGraph.clear();
        for (auto& [name, fn] : funcs) callsIn(fn->body.get(), callGraph[name]);
        for (auto& [cname, cls] : classes)
            for (auto& m : cls->methodList) callsIn(m->body.get(), callGraph["." + m->name]);
        std::set<string> done, onPath;
        std::function<bool(const string&)> cyclic = [&](const string& n) -> bool {
            if (onPath.count(n)) return true;            // 회로
            if (done.count(n) || !callGraph.count(n)) return false;
            onPath.insert(n);
            for (const string& next : callGraph[n]) if (cyclic(next)) return true;
            onPath.erase(n);
            done.insert(n);
            return false;
        };
        for (auto& [n, _] : callGraph) if (cyclic(n)) return true;
        return false;
    }

    void inferStrVars(std::vector<StmtP>& program) {
        strSites.clear(); strBanned.clear(); strVars.clear();
        for (auto& st : program) scanStr(st.get());
        for (auto& [n, sites] : strSites)
            if (!strBanned.count(n)) strVars.insert(n);
        for (bool changed = true; changed; ) {      // 아닌 것부터 걷어낸다
            changed = false;
            for (auto it = strVars.begin(); it != strVars.end(); ) {
                bool ok = true;
                for (Expr* e : strSites[*it]) if (e && !stringish(e)) { ok = false; break; }
                if (ok) ++it;
                else { it = strVars.erase(it); changed = true; }
            }
        }
    }

    // 컴파일 시점에 값이 정해지는 정수인가. -1 은 NegExpr(NumExpr) 로 파싱되므로 같이 본다.
    static bool constInt(Expr* e, double& out) {
        if (auto* n = dynamic_cast<NumExpr*>(e)) {
            if (n->v != (long long)n->v) return false;
            out = n->v; return true;
        }
        if (auto* g = dynamic_cast<NegExpr*>(e)) {
            double v;
            if (!constInt(g->inner.get(), v)) return false;
            out = -v; return true;
        }
        return false;
    }

    // print 에 그대로 넘겨도 Venos 와 같게 찍히는 식인가.
    // 아니면 _show() 로 감싼다 (딕셔너리 따옴표, 참/거짓, 5.0 표기 때문).
    bool plainSafe(Expr* e) {
        if (stringish(e)) return true;
        // not/and/or 는 int(...) 로 감싸 내보내므로 파이썬에서도 정수다
        if (dynamic_cast<NotExpr*>(e) || dynamic_cast<LogicalExpr*>(e)) return true;
        double k;
        if (constInt(e, k)) return true;
        if (auto* v = dynamic_cast<VarExpr*>(e)) return intVars.count(v->name) > 0;
        // len() 은 정수, has() 는 int(... in ...) 라 둘 다 파이썬에서도 정수로 찍힌다
        if (auto* c = dynamic_cast<CallExpr*>(e))
            return (c->name == "len" || c->name == "has") && !funcs.count(c->name);
        // 정수끼리의 + - * % 는 파이썬에서도 정수라 그대로 찍어도 된다 (/ 는 소수가 되므로 제외)
        if (auto* b = dynamic_cast<BinExpr*>(e)) {
            if (b->interpN > 0) return true;
            switch (b->op) {
                case Tok::PLUS: case Tok::MINUS: case Tok::STAR: case Tok::PERCENT:
                    return plainSafe(b->lhs.get()) && plainSafe(b->rhs.get())
                        && !stringish(b->lhs.get()) && !stringish(b->rhs.get());
                default: return false;
            }
        }
        return false;
    }
    // 본문이 이미 return 으로 끝나면 뒤에 return 0 을 붙이지 않는다
    static bool endsWithReturn(Stmt* s) {
        if (!s) return false;
        if (dynamic_cast<ReturnStmt*>(s)) return true;
        if (auto* b = dynamic_cast<BlockStmt*>(s))
            return !b->stmts.empty() && endsWithReturn(b->stmts.back().get());
        if (auto* i = dynamic_cast<IfStmt*>(s))
            return i->elseB && endsWithReturn(i->thenB.get()) && endsWithReturn(i->elseB.get());
        return false;
    }

    // 보간 체인 "이름: {x}" → f"이름: {x}" 로 되돌린다.
    // 파서가 ("" + "이름: ") + x 로 풀어놨고 루트에 조각 수가 남아 있다.
    string fstring(BinExpr* root) {
        std::vector<Expr*> parts;
        Expr* cur = root;
        for (int k = 0; k < root->interpN; k++) {
            auto* b = static_cast<BinExpr*>(cur);
            parts.push_back(b->rhs.get());
            cur = b->lhs.get();
        }
        std::reverse(parts.begin(), parts.end());

        // 값 자리를 먼저 만들어 둔다. print 와 같은 규칙 — 파이썬 표기가 새어 나올 수 있으면
        // _show() 로 감싼다 (안 감싸면 True / 5.0 / {'a': 1} / 83.33333333333333 이 찍힌다).
        std::map<Expr*, string> code;
        bool hasDq = false, hasSq = false;
        for (Expr* p : parts) {
            if (dynamic_cast<StrExpr*>(p)) continue;
            string c = plainSafe(p) ? expr(p) : need("show") + "(" + expr(p) + ")";
            hasDq = hasDq || c.find('"')  != string::npos;
            hasSq = hasSq || c.find('\'') != string::npos;
            code[p] = c;
        }
        // 파이썬 3.11 까지는 f-문자열 안에서 바깥과 같은 따옴표를 다시 못 쓴다.
        // "{replace(s, \"a\", \"b\")}" 같은 보간이 여기 걸린다 — 따옴표를 바꿔 피하고,
        // 양쪽 다 들어 있으면 f-문자열을 포기하고 이어붙이기로 낸다.
        if (hasDq && hasSq) {
            string o;
            for (Expr* p : parts) {
                if (!o.empty()) o += " + ";
                if (auto* st = dynamic_cast<StrExpr*>(p)) o += pyStr(st->s);
                else                                     o += code[p];
            }
            return o.empty() ? "\"\"" : "(" + o + ")";
        }
        const char q = hasDq ? '\'' : '"';
        string o = string("f") + q;
        for (Expr* p : parts) {
            if (auto* st = dynamic_cast<StrExpr*>(p)) {
                for (unsigned char uc : st->s) if (uc > 0x7F) { sawNonAscii = true; break; }
                for (char c : st->s) {
                    if (c == '{')       o += "{{";
                    else if (c == '}')  o += "}}";
                    else if (c == q)    { o += '\\'; o += c; }
                    else if (c == '\\') o += "\\\\";
                    else if (c == '\n') o += "\\n";
                    else if (c == '\t') o += "\\t";
                    else                o += c;
                }
            } else {
                o += "{" + code[p] + "}";
            }
        }
        return o + q;
    }

    // ---- 표현식 ----
    string expr(Expr* e) {
        if (auto* n = dynamic_cast<NumExpr*>(e)) {
            if (n->v != (long long)n->v) sawFloat = true;
            return pyNum(n->v);
        }
        if (auto* s = dynamic_cast<StrExpr*>(e))  return pyStrTracked(s->s);
        if (auto* v = dynamic_cast<VarExpr*>(e))  return pyName(v->name);
        if (auto* l = dynamic_cast<ListExpr*>(e)) {
            sawList = true;
            string o = "[";
            for (size_t i = 0; i < l->items.size(); i++) {
                if (i) o += ", ";
                o += expr(l->items[i].get());
            }
            return o + "]";
        }
        if (auto* m = dynamic_cast<MapExpr*>(e)) {
            sawMap = true;
            string o = "{";
            for (size_t i = 0; i < m->items.size(); i++) {
                if (i) o += ", ";
                o += expr(m->items[i].first.get()) + ": " + expr(m->items[i].second.get());
            }
            return o + "}";
        }
        if (auto* ix = dynamic_cast<IndexExpr*>(e))
            return indexGet(ix->target.get(), ix->index.get(), ix->line);
        // 숫자 리터럴 뒤의 점은 파이썬에서 소수점으로 읽힌다 — `5.뭐()` 는 **문법 오류**라
        // 아예 파싱도 안 되는 파이썬이 나갔다. 괄호를 씌워야 한다. (Venos 에서도 파이썬에서도
        // 숫자에 메서드·필드는 에러지만, 틀린 답보다 나쁜 건 아예 안 돌아가는 파일이다.)
        auto dotted = [&](Expr* t) {
            string o = wrap(t, P_ATOM);
            return dynamic_cast<NumExpr*>(t) ? "(" + o + ")" : o;
        };
        if (auto* f = dynamic_cast<FieldExpr*>(e))
            return dotted(f->target.get()) + "." + pyName(f->field);
        if (auto* mc = dynamic_cast<MethodCallExpr*>(e)) {
            string o = dotted(mc->target.get()) + "." + pyName(mc->method) + "(";
            for (size_t i = 0; i < mc->args.size(); i++) {
                if (i) o += ", ";
                o += expr(mc->args[i].get());
            }
            return o + ")";
        }
        if (auto* b = dynamic_cast<BinExpr*>(e)) {
            if (b->interpN > 0) return fstring(b);
            int p = precOf(b);
            string L = wrap(b->lhs.get(), p), R = wrap(b->rhs.get(), p + 1);
            // Venos 는 "나이: " + 15 를 알아서 이어붙인다. 파이썬은 아니므로 str() 을 씌운다.
            if (b->op == Tok::PLUS && (stringish(b->lhs.get()) || stringish(b->rhs.get()))) {
                if (!stringish(b->lhs.get())) L = need("show") + "(" + expr(b->lhs.get()) + ")";
                if (!stringish(b->rhs.get())) R = need("show") + "(" + expr(b->rhs.get()) + ")";
            }
            // "*" * 5 — 파이썬은 반복 횟수가 정수여야 한다 (Venos 의 수는 소수일 수 있다)
            if (b->op == Tok::STAR) {
                double k;
                if (stringish(b->lhs.get()) && !constInt(b->rhs.get(), k)) R = "int(" + expr(b->rhs.get()) + ")";
                if (stringish(b->rhs.get()) && !constInt(b->lhs.get(), k)) L = "int(" + expr(b->lhs.get()) + ")";
            }
            const char* op = "+";
            switch (b->op) {
                case Tok::PLUS: op = "+";  break;
                case Tok::MINUS:op = "-";  break;
                case Tok::STAR: op = "*";  break;
                case Tok::SLASH:op = "/";  sawFloat = true; break;
                case Tok::PERCENT: op = "%"; break;
                case Tok::EQ:   op = "=="; break;
                case Tok::NEQ:  op = "!="; break;
                case Tok::LT:   op = "<";  break;
                case Tok::GT:   op = ">";  break;
                case Tok::LE:   op = "<="; break;
                case Tok::GE:   op = ">="; break;
                default: throw nope(b->line, "지원하지 않는 연산자");
            }
            return L + " " + op + " " + R;
        }
        if (auto* n = dynamic_cast<NegExpr*>(e))  return "-" + wrap(n->inner.get(), P_UNARY);
        // Venos 의 not/and/or 는 1/0 을 낸다 — 파이썬 bool 이 그대로 찍히지 않도록 int() 로 맞춘다.
        // 안쪽은 참/거짓만 보면 되므로 조건 형태로 낸다 (int(not int(not a)) 같은 겹침 방지).
        if (auto* n = dynamic_cast<NotExpr*>(e))
            return "int(not " + condWrap(n->inner.get(), P_NOT + 1) + ")";
        if (auto* lg = dynamic_cast<LogicalExpr*>(e)) {
            int p = (lg->op == Tok::AND) ? P_AND : P_OR;   // 내부 자식용 (밖에서는 원자)
            // 파이썬의 and/or 는 피연산자를 그대로 돌려준다 — 1 or 2 는 2 다.
            // Venos 는 1/0 이므로 이미 0/1 인 식이 아니면 bool() 로 눌러 둔다.
            auto side = [&](Expr* x, int pp) {
                return boolish(x) ? condWrap(x, pp) : "bool(" + expr(x) + ")";
            };
            return "int(" + side(lg->lhs.get(), p)
                 + ((lg->op == Tok::AND) ? " and " : " or ")
                 + side(lg->rhs.get(), p + 1) + ")";
        }
        if (auto* in = dynamic_cast<InputExpr*>(e)) {
            sawFloat = true;   // 사용자가 1.5 를 칠 수도 있다
            sawInput = true;
            return need("input") + "(" + (in->prompt.empty() ? "" : pyStr(in->prompt)) + ")";
        }
        if (auto* c = dynamic_cast<CallExpr*>(e)) return call(c);
        throw LangError("파이썬으로 변환할 수 없는 식이 있습니다");
    }

    // xs[1] 은 첫 번째, d["키"] 는 키 조회 — 리터럴이면 그 자리에서 정하고,
    // 변수라서 알 수 없으면 도우미로 넘긴다.
    string indexGet(Expr* target, Expr* index, int line) {
        sawIndex = true;
        string T = wrap(target, P_ATOM);
        if (dynamic_cast<StrExpr*>(index)) return T + "[" + expr(index) + "]";
        if (auto* n = dynamic_cast<NumExpr*>(index)) {
            // 1부터를 0부터로 옮기므로 0 은 파이썬에서 [-1] 이 된다 — 에러가 아니라
            // "마지막 원소"가 조용히 나온다. 초보자 최빈 실수 1번이라 여기서 막는다.
            if (n->v < 1)
                throw err(line, "리스트와 문자열의 인덱스는 1부터입니다 (지금 "
                                + pyNum(n->v) + ") — 파이썬으로 옮기면 뒤에서 세는 뜻이 되어 버립니다");
            return T + "[" + pyNum(n->v - 1) + "]";
        }
        if (!sawMap) return T + "[" + wrap(index, P_MUL) + " - 1]";   // 딕셔너리가 없으면 리스트뿐
        return need("idx") + "(" + expr(target) + ", " + expr(index) + ")";
    }

    string call(CallExpr* c) {
        auto A = [&](size_t i) { return expr(c->args[i].get()); };
        // .메서드() 를 붙일 인자는 원자로 만들어야 한다.
        // upper(a + b) → (a + b).upper()  (괄호가 없으면 b.upper() 가 되어 조용히 틀린다)
        auto atom = [&](size_t i) { return wrap(c->args[i].get(), P_ATOM); };
        size_t n = c->args.size();
        const string& f = c->name;
        auto argsJoined = [&]() {
            string o;
            for (size_t i = 0; i < n; i++) { if (i) o += ", "; o += A(i); }
            return o;
        };
        // 사용자 함수 / 클래스 생성자
        if (funcs.count(f) || classes.count(f)) return pyName(f) + "(" + argsJoined() + ")";

        auto need2 = [&](size_t want) {
            if (n != want)
                throw err(c->line, f + "() 는 인자 " + std::to_string(want)
                                     + "개가 필요합니다 (지금 " + std::to_string(n) + "개)");
        };
        // 파이썬이 Venos 와 다르게 답하는 인자들. 리터럴로 적혀 있으면 여기서 거절한다 —
        // 틀린 파이썬을 내느니 줄 번호를 대고 거절하는 게 약속이다.
        auto noEmptyStr = [&](size_t i, const char* what) {
            auto* lit = dynamic_cast<StrExpr*>(c->args[i].get());
            if (lit && lit->s.empty())
                throw err(c->line, string(what) + josa(what, "은", "는") + " 비어 있을 수 없습니다"
                                   " (파이썬은 글자 사이마다 끼워 넣어 다른 답을 냅니다)");
        };
        auto numbersOnly = [&](size_t i) {
            if (dynamic_cast<StrExpr*>(c->args[i].get()))
                throw err(c->line, f + "() 에는 수만 넣을 수 있습니다"
                                      " (파이썬은 문자열도 비교해 다른 답을 냅니다)");
        };
        if (f == "len")   { need2(1); return "len(" + A(0) + ")"; }
        if (f == "abs")   { need2(1); return "abs(" + A(0) + ")"; }
        if (f == "min")   { need2(2); numbersOnly(0); numbersOnly(1); return "min(" + A(0) + ", " + A(1) + ")"; }
        if (f == "max")   { need2(2); numbersOnly(0); numbersOnly(1); return "max(" + A(0) + ", " + A(1) + ")"; }
        if (f == "floor") {
            need2(1);
            // 교과서의 중간값 계산 floor((왼쪽+오른쪽)/2) 는 파이썬에서 // 로 쓴다.
            if (auto* d = floorDiv(c))
                return wrap(d->lhs.get(), P_MUL) + " // " + wrap(d->rhs.get(), P_MUL + 1);
            imports.insert("math");
            return "math.floor(" + A(0) + ")";
        }
        if (f == "ceil")  { need2(1); imports.insert("math"); return "math.ceil("  + A(0) + ")"; }
        if (f == "sqrt")  { need2(1); imports.insert("math"); sawFloat = true; return "math.sqrt("  + A(0) + ")"; }
        if (f == "round") {
            if (n != 1 && n != 2)
                throw err(c->line, "round() 는 인자 1~2개가 필요합니다 (지금 " + std::to_string(n) + "개)");
            imports.insert("math");
            if (n == 2) sawFloat = true;
            return need("round") + "(" + A(0) + (n == 2 ? ", " + A(1) : "") + ")";
        }
        if (f == "reverse") { need2(1); if (!stringish(c->args[0].get())) sawList = true;
                              return need("reverse") + "(" + A(0) + ")"; }
        if (f == "random"){ need2(2); imports.insert("random"); return need("random") + "(" + A(0) + ", " + A(1) + ")"; }
        if (f == "time")  { need2(0); imports.insert("time"); sawFloat = true; return "time.time()"; }
        if (f == "num")   { need2(1); sawFloat = true; return need("num")  + "(" + A(0) + ")"; }
        if (f == "str")   { need2(1); return need("show") + "(" + A(0) + ")"; }
        if (f == "push")  { need2(2); sawList = true; return need("push") + "(" + A(0) + ", " + A(1) + ")"; }
        if (f == "pop")   { need2(1); return atom(0) + ".pop()"; }
        if (f == "sort")  { need2(1); sawList = true; return need("sort") + "(" + A(0) + ")"; }
        if (f == "keys")  { need2(1); sawMap = true; sawList = true; return "sorted(" + atom(0) + ".keys())"; }
        // Venos 의 has() 는 딕셔너리와 리스트에만 된다. 파이썬의 `in` 은 문자열에도 되어서
        // has("abc", "b") 가 여기서는 에러, 파이썬에서는 1 이다 — 에러가 답으로 바뀌는 쪽이다.
        // 문자열인 줄 알 수 있으면 거절한다. `int(x in d)` 라는 표기는 그대로 남는다.
        if (f == "has")   { need2(2); sawMap = true;
                            if (stringish(c->args[0].get()))
                                throw nope(c->line, "has() 는 딕셔너리나 리스트에만 쓸 수 있습니다"
                                                    " (파이썬의 in 은 문자열에도 되어 다른 답을 냅니다"
                                                    " — 문자열 안을 찾으려면 find(문자열, 조각) 을 쓰세요)");
                            return "int(" + wrap(c->args[1].get(), P_CMP + 1) + " in "
                                          + wrap(c->args[0].get(), P_CMP + 1) + ")"; }
        if (f == "remove"){ need2(2); return need("remove") + "(" + A(0) + ", " + A(1) + ")"; }
        if (f == "split") { need2(2); sawList = true; return atom(0) + ".split(" + A(1) + ")"; }
        if (f == "join")  { need2(2); return need("join") + "(" + A(0) + ", " + A(1) + ")"; }
        // Venos 의 upper()/lower() 는 영문자만 바꾼다. 파이썬의 str.upper() 는 유니코드
        // 전체를 바꿔서 é→É, ß→SS(길이가 늘어난다!), 터키어 ı→I 까지 간다.
        // 한글·이모지·숫자에는 둘 다 손대지 않으므로 교과서 프로그램에서는 같은 답이 나온다.
        // 도우미로 감싸면 s.upper() 라는 표기를 못 배우게 되므로, 리스트 인덱스와 같은
        // 판단으로 그대로 두고 **생성 파일 머리말에 차이를 적는다** (STRATEGY §6).
        if (f == "upper") { need2(1); sawCase = true; return atom(0) + ".upper()"; }
        if (f == "lower") { need2(1); sawCase = true; return atom(0) + ".lower()"; }
        // find 는 + 1 이 붙으므로 통째로 괄호를 씌운다 (find(s,x) * 10 이 s.find(x) + 1 * 10 이 되면 안 된다)
        // 찾을 문자열이 리터럴이면 s.find(x) + 1 이 그대로 읽힌다. 변수면 빈 문자열이
        // 들어올 수 있고, 그때 파이썬은 0 을 주지만 Venos 는 에러다 — 검사하는 쪽으로 보낸다.
        if (f == "find")  { need2(2); noEmptyStr(1, "find() 로 찾을 문자열");
                            if (stringish(c->args[0].get()) && dynamic_cast<StrExpr*>(c->args[1].get()))
                                return "(" + atom(0) + ".find(" + A(1) + ") + 1)";
                            return need("find") + "(" + A(0) + ", " + A(1) + ")"; }
        if (f == "replace"){need2(3); noEmptyStr(1, "replace() 의 바꿀 문자열");
                            if (dynamic_cast<StrExpr*>(c->args[1].get()))
                                return atom(0) + ".replace(" + A(1) + ", " + A(2) + ")";
                            return need("replace") + "(" + A(0) + ", " + A(1) + ", " + A(2) + ")"; }
        if (f == "substr"){ need2(3); return need("substr") + "(" + A(0) + ", " + A(1) + ", " + A(2) + ")"; }
        if (f == "readfile")  { need2(1); return need("readfile")   + "(" + A(0) + ")"; }
        if (f == "writefile") { need2(2); return need("writefile")  + "(" + A(0) + ", " + A(1) + ")"; }
        if (f == "appendfile"){ need2(2); return need("appendfile") + "(" + A(0) + ", " + A(1) + ")"; }
        if (f == "exists"){ need2(1); imports.insert("os"); return "int(os.path.exists(" + A(0) + "))"; }
        if (f == "exit")  { need2(0); imports.insert("sys"); return need("exit") + "()"; }
        if (f == "error") { need2(1); return need("error") + "(" + A(0) + ")"; }
        if (f == "copy")  { need2(1); imports.insert("copy"); return "copy.deepcopy(" + A(0) + ")"; }
        throw err(c->line, "정의되지 않은 함수: " + f);
    }

    // ---- 문장 ----
    static string pad(int d) { return string(d * 4, ' '); }

    // 대입 대상이 전역이면 함수 안에서 global 선언이 필요하다.
    // 겸해서 "선언되지 않은 변수에 대입"을 여기서 막는다 — 세 백엔드가 갈라지던 자리다:
    // 인터프리터는 잡을 수 있는 에러, build 는 빌드 거절, 파이썬은 **그냥 새 변수를 만든다**.
    // try 안에서 났을 때 인터프리터는 "잡음"을 찍고 파이썬은 대입을 해 버려 다른 프로그램이 된다.
    void noteAssign(const string& name, int line) {
        if (!(inFunc && localSet.count(name)) && !globalSet.count(name)) {
            string hint = suggestName(name, visibleNames());
            if (hint.empty()) hint = "  (" + KW_LET + " " + name + " = ... 로 먼저 선언하세요)";
            throw err(line, "선언되지 않은 변수에 대입: " + name + hint);
        }
        if (inFunc && !localSet.count(name) && globalSet.count(name)) touchedGlobals.insert(name);
        // for 루프 변수에 다시 대입하면 더 이상 정수라고 볼 수 없다
        intVars.erase(name);
    }

    // 생성된 파이썬 조각 안에 그 이름이 **낱말로** 나오는가 (부분 문자열 오탐 방지)
    static bool mentionsName(const string& text, const string& name) {
        auto idChar = [](unsigned char c) { return isalnum(c) || c == '_' || c >= 0x80; };
        for (size_t i = text.find(name); i != string::npos; i = text.find(name, i + 1)) {
            bool lOK = (i == 0) || !idChar(text[i - 1]);
            size_t e = i + name.size();
            bool rOK = (e >= text.size()) || !idChar(text[e]);
            if (lOK && rOK) return true;
        }
        return false;
    }

    std::vector<string> visibleNames() const {
        std::vector<string> out(globalSet.begin(), globalSet.end());
        if (inFunc) out.insert(out.end(), localSet.begin(), localSet.end());
        return out;
    }

    // 경로 대입의 앞부분: xs[1][2] / obj.필드 를 파이썬 좌변으로
    string lvalue(const string& name, const std::vector<Accessor>& path) {
        string cur = pyName(name);
        for (auto& a : path) {
            if (a.isField) { cur += "." + pyName(a.field); continue; }
            Expr* ix = a.index.get();
            if (dynamic_cast<StrExpr*>(ix))            cur += "[" + expr(ix) + "]";
            else if (auto* nn = dynamic_cast<NumExpr*>(ix)) cur += "[" + pyNum(nn->v - 1) + "]";
            else if (!sawMap)                          cur += "[" + wrap(ix, P_MUL) + " - 1]";
            else                                       cur += "[" + need("k") + "(" + cur + ", " + expr(ix) + ")]";
        }
        return cur;
    }

    void stmt(Stmt* s, std::ostringstream& o, int d) {
        if (auto* l = dynamic_cast<LetStmt*>(s)) {
            if (inFunc) localSet.insert(l->name);
            intVars.erase(l->name);
            o << pad(d) << pyName(l->name) << " = " << expr(l->val.get()) << "\n";
            return;
        }
        if (auto* a = dynamic_cast<AssignStmt*>(s)) {
            noteAssign(a->name, a->line);
            // 파서가 x += 1 을 x = x + 1 로 풀어놓는다 — 읽기 좋게 되돌린다
            if (auto* b = dynamic_cast<BinExpr*>(a->val.get())) {
                auto* lv = dynamic_cast<VarExpr*>(b->lhs.get());
                const char* cop = nullptr;
                if (b->interpN == 0 && lv && lv->name == a->name) {
                    switch (b->op) {
                        case Tok::PLUS:  cop = "+="; break;
                        case Tok::MINUS: cop = "-="; break;
                        case Tok::STAR:  cop = "*="; break;
                        case Tok::SLASH: cop = "/="; break;
                        default: break;
                    }
                }
                if (cop) {
                    o << pad(d) << pyName(a->name) << " " << cop << " "
                      << wrap(b->rhs.get(), P_ADD + 1) << "\n";
                    return;
                }
            }
            o << pad(d) << pyName(a->name) << " = " << expr(a->val.get()) << "\n";
            return;
        }
        if (auto* pa = dynamic_cast<PathAssignStmt*>(s)) {
            noteAssign(pa->name, pa->line);
            o << pad(d) << lvalue(pa->name, pa->path) << " = " << expr(pa->val.get()) << "\n";
            return;
        }
        if (auto* pc = dynamic_cast<PathCompoundStmt*>(s)) {
            noteAssign(pc->name, pc->line);
            string slot = lvalue(pc->name, pc->path);
            const char* op = "+";
            switch (pc->op) {
                case Tok::PLUS: op = "+="; break;
                case Tok::MINUS:op = "-="; break;
                case Tok::STAR: op = "*="; break;
                case Tok::SLASH:op = "/="; break;
                default: throw nope(pc->line, "복합 대입 연산자");
            }
            o << pad(d) << slot << " " << op << " " << expr(pc->rhs.get()) << "\n";
            return;
        }
        if (auto* p = dynamic_cast<PrintStmt*>(s)) {

            o << pad(d) << "print(";
            for (size_t i = 0; i < p->vals.size(); i++) {
                if (i) o << ", ";
                Expr* v = p->vals[i].get();
                // Venos 와 똑같이 찍히는 게 확실하면 그대로, 아니면 _show() 로 맞춘다
                o << (plainSafe(v) ? expr(v) : need("show") + "(" + expr(v) + ")");
            }
            o << ")\n";
            return;
        }
        if (auto* es = dynamic_cast<ExprStmt*>(s)) {
            // 값을 안 쓰는 자리라면 파이썬이 실제로 쓰는 모양으로 낸다
            // (_push(xs, v) 가 아니라 xs.append(v))
            if (auto* c = dynamic_cast<CallExpr*>(es->e.get())) {
                if (!funcs.count(c->name) && !classes.count(c->name)) {
                    if (c->name == "push" && c->args.size() == 2) {
                        sawList = true;
                        o << pad(d) << wrap(c->args[0].get(), P_ATOM)
                          << ".append(" << expr(c->args[1].get()) << ")\n";
                        return;
                    }
                    if (c->name == "sort" && c->args.size() == 1) {
                        sawList = true;
                        o << pad(d) << wrap(c->args[0].get(), P_ATOM) << ".sort()\n";
                        return;
                    }
                    if (c->name == "reverse" && c->args.size() == 1
                        && !stringish(c->args[0].get())) {
                        sawList = true;
                        o << pad(d) << wrap(c->args[0].get(), P_ATOM) << ".reverse()\n";
                        return;
                    }
                }
            }
            o << pad(d) << expr(es->e.get()) << "\n";
            return;
        }
        if (auto* b = dynamic_cast<BlockStmt*>(s)) {
            for (auto& c : b->stmts) stmt(c.get(), o, d);
            return;
        }
        if (auto* i = dynamic_cast<IfStmt*>(s)) {
            o << pad(d) << "if " << cond(i->cond.get()) << ":\n";
            body(i->thenB.get(), o, d + 1);
            Stmt* els = i->elseB.get();
            while (els) {
                if (auto* chain = dynamic_cast<IfStmt*>(els)) {   // else if → elif
                    o << pad(d) << "elif " << cond(chain->cond.get()) << ":\n";
                    body(chain->thenB.get(), o, d + 1);
                    els = chain->elseB.get();
                } else {
                    o << pad(d) << "else:\n";
                    body(els, o, d + 1);
                    els = nullptr;
                }
            }
            return;
        }
        if (auto* t = dynamic_cast<TryStmt*>(s)) {
            o << pad(d) << "try:\n";
            body(t->tryB.get(), o, d + 1);
            o << pad(d) << "except Exception as _e:\n";
            if (inFunc) localSet.insert(t->var);
            o << pad(d + 1) << pyName(t->var) << " = str(_e)\n";
            body(t->catchB.get(), o, d + 1);
            return;
        }
        if (auto* w = dynamic_cast<WhileStmt*>(s)) {
            o << pad(d) << "while " << cond(w->cond.get()) << ":\n";
            body(w->body.get(), o, d + 1);
            return;
        }
        if (auto* f = dynamic_cast<ForStmt*>(s)) {
            if (inFunc) localSet.insert(f->var);
            o << pad(d) << "for " << pyName(f->var) << " in " << rangeOf(f) << ":\n";
            bool had = intVars.count(f->var) > 0;
            // 진짜 range() 를 쓸 때만 정수다. step 0.5 처럼 소수가 섞이면 _rng 가 소수를 내준다.
            bool isInt = lastRangeIsInt;
            if (isInt) intVars.insert(f->var);
            body(f->body.get(), o, d + 1);
            if (!had) intVars.erase(f->var);
            return;
        }
        if (auto* fe = dynamic_cast<ForEachStmt*>(s)) {
            if (inFunc) localSet.insert(fe->var);
            Expr* it = fe->iter.get();
            // 몸통을 먼저 만들어 둔다 — 그 안에서 도는 리스트를 건드리는지 봐야 하기 때문.
            std::ostringstream fb;
            body(fe->body.get(), fb, d + 1);
            // 리터럴이면 그대로 돈다. 변수면 딕셔너리일 수 있어 도우미를 거친다
            // (Venos 는 딕셔너리를 키 정렬 순서로 순회한다).
            bool viaIter = sawMap && !dynamic_cast<ListExpr*>(it) && !dynamic_cast<StrExpr*>(it);
            string src = viaIter ? need("iter") + "(" + expr(it) + ")" : expr(it);
            // Venos 의 for..in 은 **루프에 들어갈 때의 리스트**를 돈다. 파이썬의 for 는
            // 살아 있는 리스트를 돌기 때문에, 몸통에서 push 하면 무한 루프가 된다
            // (생성 퍼저가 찾았다 — 에러 하나 없이 답만 달라진다).
            // _iter 를 거치면 거기서 이미 사본을 주므로 덧씌우지 않는다. 그 밖에는
            // 몸통이 그 이름을 건드릴 때만 감싼다 — 읽기 좋은 쪽을 지킨다.
            if (!viaIter)
                if (auto* v = dynamic_cast<VarExpr*>(it))
                    if (mentionsName(fb.str(), pyName(v->name))) src = "list(" + src + ")";
            o << pad(d) << "for " << pyName(fe->var) << " in " << src << ":\n";
            o << fb.str();
            return;
        }
        if (dynamic_cast<BreakStmt*>(s))    { o << pad(d) << "break\n";    return; }
        if (dynamic_cast<ContinueStmt*>(s)) { o << pad(d) << "continue\n"; return; }
        if (auto* r = dynamic_cast<ReturnStmt*>(s)) {
            o << pad(d) << "return " << (r->val ? expr(r->val.get()) : "0") << "\n";
            return;
        }
        if (dynamic_cast<FuncStmt*>(s))  return;   // 최상위로 끌어올려 따로 낸다
        if (dynamic_cast<ClassStmt*>(s)) return;
        throw LangError("파이썬으로 변환할 수 없는 문장이 있습니다");
    }

    // for i = a to b step s  →  range. Venos 는 양끝을 포함하므로 끝값을 한 칸 민다.
    string rangeOf(ForStmt* f) {
        string a = expr(f->start.get()), b = expr(f->end.get());
        double sa, sb, st;
        bool ka = constInt(f->start.get(), sa), kb = constInt(f->end.get(), sb);
        lastRangeIsInt = true;
        if (!f->step) {
            if (ka && kb)                       // 둘 다 상수면 방향이 확정된다
                return sa <= sb ? "range(" + a + ", " + pyNum(sb + 1) + ")"
                                : "range(" + a + ", " + pyNum(sb - 1) + ", -1)";
            // 방향이 실행할 때 정해지므로 range() 로는 못 낸다 (_rng 가 정해 준다).
            // 그래도 양 끝이 정수인 게 확실하면 _rng 는 range 를 돌려주므로 i 는 정수다
            // (_rng 는 a, b, s 가 모두 정수일 때만 range 를 낸다). for i = 1 to len(xs)
            // 가 교과서에서 제일 흔한 모양이라, 여기서 정수라고 말해 주면 "{i}번" 이
            // _show(i) 없이 그대로 나간다.
            lastRangeIsInt = intish(f->start.get()) && intish(f->end.get());
            return need("rng") + "(" + a + ", " + b + ")";
        }
        // range() 는 정수만 받는다 — 시작값이 정수라고 확신할 수 있을 때만 쓴다.
        // (for i = 어떤소수 to 10 step 2 를 range 로 내면 파이썬이 TypeError 를 낸다)
        if (kb && (ka || intish(f->start.get())) && constInt(f->step.get(), st) && st != 0)
            return "range(" + a + ", " + pyNum(sb + (st > 0 ? 1 : -1)) + ", " + pyNum(st) + ")";
        lastRangeIsInt = intish(f->start.get()) && intish(f->end.get())
                      && intish(f->step.get());
        return need("rng") + "(" + a + ", " + b + ", " + expr(f->step.get()) + ")";
    }
    // 파이썬에서 정수로 나오는 게 확실한 식인가 (range() 에 그대로 넣어도 되는가)
    // floor(a / b) 를 파이썬의 a // b 로 낼 수 있는가. **양쪽이 정수로 보일 때만** 이다 —
    // 소수끼리면 파이썬의 // 는 3.0 같은 소수를 내고, 그게 인덱스 자리에 오면 TypeError 다
    // (math.floor 는 늘 정수를 낸다). 학생이 floor 라는 함수를 직접 만들었으면 손대지 않는다.
    BinExpr* floorDiv(Expr* e) {
        auto* c = dynamic_cast<CallExpr*>(e);
        if (!c || c->name != "floor" || c->args.size() != 1 || funcs.count("floor")) return nullptr;
        auto* b = dynamic_cast<BinExpr*>(c->args[0].get());
        if (!b || b->op != Tok::SLASH || b->interpN > 0) return nullptr;
        if (!intish(b->lhs.get()) || !intish(b->rhs.get())) return nullptr;
        return b;
    }

    bool intish(Expr* e) {
        double k;
        if (constInt(e, k)) return true;
        if (auto* v = dynamic_cast<VarExpr*>(e)) return intVars.count(v->name) > 0;
        if (auto* n = dynamic_cast<NegExpr*>(e)) return intish(n->inner.get());
        if (auto* c = dynamic_cast<CallExpr*>(e)) {
            if (funcs.count(c->name) || classes.count(c->name)) return false;
            if (c->name == "len"    && c->args.size() == 1) return true;
            if (c->name == "floor"  && c->args.size() == 1) return true;
            if (c->name == "ceil"   && c->args.size() == 1) return true;
            if (c->name == "round"  && c->args.size() == 1) return true;
            if (c->name == "random" && c->args.size() == 2) return true;
            return false;
        }
        if (auto* b = dynamic_cast<BinExpr*>(e)) {
            if (b->interpN > 0) return false;
            switch (b->op) {
                case Tok::PLUS: case Tok::MINUS: case Tok::STAR: case Tok::PERCENT:
                    return intish(b->lhs.get()) && intish(b->rhs.get());
                default: return false;     // / 는 소수가 된다
            }
        }
        return false;
    }

    // 빈 블록은 파이썬에서 pass 가 필요하다
    void body(Stmt* s, std::ostringstream& o, int d) {
        std::ostringstream tmp;
        stmt(s, tmp, d);
        if (tmp.str().empty()) o << pad(d) << "pass\n";
        else                   o << tmp.str();
    }

    // ---- 함수/클래스 수집 (중첩 func 도 최상위로) ----
    void collect(Stmt* s) {
        if (auto* c = dynamic_cast<ClassStmt*>(s)) {
            classes[c->name] = c;
            for (auto& m : c->methodList) collect(m->body.get());
            return;
        }
        if (auto* f = dynamic_cast<FuncStmt*>(s)) { funcs[f->name] = f; collect(f->body.get()); return; }
        if (auto* b = dynamic_cast<BlockStmt*>(s)) { for (auto& c : b->stmts) collect(c.get()); return; }
        if (auto* i = dynamic_cast<IfStmt*>(s)) {
            collect(i->thenB.get());
            if (i->elseB) collect(i->elseB.get());
            return;
        }
        if (auto* w = dynamic_cast<WhileStmt*>(s))     { collect(w->body.get()); return; }
        if (auto* f = dynamic_cast<ForStmt*>(s))       { collect(f->body.get()); return; }
        if (auto* fe = dynamic_cast<ForEachStmt*>(s))  { collect(fe->body.get()); return; }
        if (auto* t = dynamic_cast<TryStmt*>(s))       { collect(t->tryB.get()); collect(t->catchB.get()); return; }
    }
    void collectVars(Stmt* s, std::set<string>& out) {
        if (auto* l = dynamic_cast<LetStmt*>(s))   { out.insert(l->name); return; }
        if (auto* f = dynamic_cast<ForStmt*>(s))   { out.insert(f->var); collectVars(f->body.get(), out); return; }
        if (auto* fe = dynamic_cast<ForEachStmt*>(s)) { out.insert(fe->var); collectVars(fe->body.get(), out); return; }
        if (auto* b = dynamic_cast<BlockStmt*>(s)) { for (auto& c : b->stmts) collectVars(c.get(), out); return; }
        if (auto* i = dynamic_cast<IfStmt*>(s)) {
            collectVars(i->thenB.get(), out);
            if (i->elseB) collectVars(i->elseB.get(), out);
            return;
        }
        if (auto* w = dynamic_cast<WhileStmt*>(s)) { collectVars(w->body.get(), out); return; }
        if (auto* t = dynamic_cast<TryStmt*>(s)) {
            out.insert(t->var);
            collectVars(t->tryB.get(), out);
            collectVars(t->catchB.get(), out);
            return;
        }
    }

    string defOf(FuncStmt* fn, const string& name, bool method, int d, bool ctor = false) {
        inFunc = true;
        localSet.clear();
        touchedGlobals.clear();
        if (method) localSet.insert("self");
        for (auto& p : fn->params) localSet.insert(p);
        collectVars(fn->body.get(), localSet);

        std::ostringstream fb;
        stmt(fn->body.get(), fb, d + 1);

        std::ostringstream o;
        o << pad(d) << "def " << name << "(";
        bool first = true;
        if (method) { o << "self"; first = false; }
        for (auto& p : fn->params) { if (!first) o << ", "; first = false; o << pyName(p); }
        o << "):\n";
        if (!touchedGlobals.empty()) {
            o << pad(d + 1) << "global ";
            bool f1 = true;
            for (auto& g : touchedGlobals) { if (!f1) o << ", "; f1 = false; o << pyName(g); }
            o << "\n";
        }
        o << (fb.str().empty() ? pad(d + 1) + "pass\n" : fb.str());
        // 파이썬 __init__ 은 값을 돌려주면 TypeError 다. 그 밖에는 Venos 처럼 기본 0 을 돌려준다.
        if (!ctor && !endsWithReturn(fn->body.get())) o << pad(d + 1) << "return 0\n";
        inFunc = false;
        localSet.clear();
        touchedGlobals.clear();
        return o.str();
    }

    string generate(std::vector<StmtP>& program) {
        for (auto& s : program) collect(s.get());
        for (auto& s : program) collectVars(s.get(), globalSet);
        inferStrVars(program);

        // 본문을 두 번 만든다. 1차는 딕셔너리가 등장하는지(sawMap)와 필요한 도우미를 알아내는 용도 —
        // 딕셔너리가 아예 없는 프로그램이면 _idx/_iter 같은 도우미 없이 훨씬 읽기 좋은 코드가 나온다.
        std::ostringstream defs, main;
        auto buildAll = [&](std::ostringstream& defsOut, std::ostringstream& mainOut) {
            for (auto& [cname, cls] : classes) {
                defsOut << "class " << pyName(cname) << ":\n";
                for (auto& m : cls->methodList)
                    defsOut << defOf(m.get(), m->name == "init" ? "__init__" : pyName(m->name),
                                     true, 1, m->name == "init") << "\n";
                // Venos 는 객체를 내용으로 보여주고 내용으로 비교한다 — 파이썬 기본 동작과 달라 맞춰 준다
                defsOut << pad(1) << "def __str__(self):\n"
                        << pad(2) << "return " << need("show") << "(self)\n\n"
                        << pad(1) << "def __eq__(self, other):\n"
                        << pad(2) << "return type(self) is type(other) and self.__dict__ == other.__dict__\n\n";
            }
            for (auto& [name, fn] : funcs) defsOut << defOf(fn, pyName(name), false, 0) << "\n";
            for (auto& st : program) stmt(st.get(), mainOut, 0);
        };
        {
            std::ostringstream d0, m0;
            buildAll(d0, m0);          // 1차: 버리는 통과
        }
        helpers.clear();
        imports.clear();
        buildAll(defs, main);
        bool deepRecursion = hasRecursion();
        if (deepRecursion || sawNonAscii || sawInput) imports.insert("sys");

        std::ostringstream out;
        out << "# 이 파일은 Venos 프로그램을 파이썬으로 옮긴 것입니다 (venos topython).\n";
        int noteN = 0;
        if (sawIndex || sawFloat || deepRecursion || !helpers.empty())
            out << "#\n# Venos 와 파이썬이 다른 점 — 숨기지 않고 적어 둡니다:\n";
        if (sawIndex)
            out << "#   " << ++noteN << ") 리스트를 Venos 는 1번부터, 파이썬은 0번부터 셉니다."
                   " 그래서 xs[1] 이 xs[0] 이 됩니다.\n"
                   "#      번호가 1 아래로 내려가면 Venos 는 에러를 내지만 파이썬은 뒤에서부터 셉니다"
                   " (xs[0] 이 마지막 원소가 됩니다).\n";
        if (sawFloat)
            out << "#   " << ++noteN << ") 소수를 보여주는 방식이 다릅니다. Venos 는 5.0 을 5 로,"
                   " 91.66666...을 91.6667 로\n"
                   "#      줄여서 보여주지만 파이썬은 있는 그대로 보여줍니다.\n";
        if (sawCase)
            out << "#   " << ++noteN << ") upper()/lower() 가 바꾸는 범위가 다릅니다. Venos 는 영문자만"
                   " 바꾸지만\n"
                   "#      파이썬은 é→É 처럼 유니코드 글자도 바꿉니다 (독일어 ß 는 SS 가 되어 길이까지 늘어납니다).\n"
                   "#      한글·숫자·이모지에는 둘 다 손대지 않습니다.\n";
        if (deepRecursion)
            out << "#   " << ++noteN << ") 재귀 깊이 한도가 다릅니다 — Venos 는 "
                << RECURSION_DESKTOP << "번, 파이썬은 기본 1000번이라\n"
                   "#      맨 위에서 sys.setrecursionlimit 으로 맞춰 두었습니다.\n";
        if (!helpers.empty())
            out << "#   " << ++noteN << ") 밑줄로 시작하는 _이름 함수들은 Venos 와 똑같이 보이게 하려고"
                   " 붙인 것뿐이니\n"
                   "#      파이썬을 배울 때는 신경 쓰지 않아도 됩니다.\n";
        // _isnum 이 정규식을 쓴다 (num/input 을 쓴 프로그램에만 붙는다)
        if (helpers.count("num") || helpers.count("input")) imports.insert("re");
        if (!imports.empty()) {
            out << "\n";
            for (auto& m : imports) out << "import " << m << "\n";
        }
        // 윈도우 파이썬은 콘솔·파이프를 로케일 코드페이지로 읽고 쓴다. 그대로 두면
        // 한글 출력이 UnicodeEncodeError 로 죽고, 한글 입력은 글자가 깨져 들어온다.
        if (sawNonAscii || sawInput) {
            out << "\n";
            if (sawNonAscii)
                out << "sys.stdout.reconfigure(encoding=\"utf-8\")"
                       "   # 윈도우 기본 인코딩에서 한글이 깨지지 않게\n";
            if (sawInput)
                out << "sys.stdin.reconfigure(encoding=\"utf-8\")\n";
        }
        if (deepRecursion)
            out << "\nsys.setrecursionlimit(" << (RECURSION_DESKTOP + 1000) << ")"
                   "   # Venos 는 " << RECURSION_DESKTOP << "번까지 허용, 파이썬 기본값은 1000\n";
        string help = helperSource();
        if (!help.empty()) out << "\n" << help;
        out << "\n";
        if (!defs.str().empty()) out << defs.str();
        out << main.str();
        return out.str();
    }

    // 실제로 쓴 도우미만 낸다 — 안 쓰면 한 줄도 안 붙는다
    string helperSource() {
        // _show 는 이 프로그램에 실제로 나올 수 있는 값 종류만 다루도록 조립한다.
        // 리스트도 딕셔너리도 클래스도 없는 프로그램이면 네 줄로 끝난다.
        string show = "def _show(v):\n"
                      "    if isinstance(v, str): return v\n"
                      "    if isinstance(v, bool): return \"1\" if v else \"0\"\n";
        if (sawList)
            show += "    if isinstance(v, list): return \"[\" + \", \".join(_q(x) for x in v) + \"]\"\n";
        if (sawMap)
            show += "    if isinstance(v, dict): return \"{\" + \", \".join('\"%s\": %s' % (k, _q(v[k])) for k in sorted(v)) + \"}\"\n";
        if (!classes.empty())
            show += "    if hasattr(v, \"__dict__\"):\n"
                    "        d = v.__dict__\n"
                    "        return type(v).__name__ + \"{\" + \", \".join('\"%s\": %s' % (k, _q(d[k])) for k in sorted(d)) + \"}\"\n";
        show += "    if isinstance(v, float):\n"
                "        return str(int(v)) if abs(v) < 9e18 and v == int(v) else \"%g\" % v\n"
                "    return str(v)\n";
        if (sawList || sawMap || !classes.empty())
            show += "def _q(v):\n"
                    "    return '\"' + v + '\"' if isinstance(v, str) else _show(v)\n";

        std::map<string, const char*> SRC = {
            // 파이썬의 float() 은 Venos 가 안 받는 것을 받는다: 1_000 은 1000 이 되고
            // inf/nan 도 통과한다. 그대로 두면 같은 프로그램이 다른 답을 낸다 —
            // 세 백엔드가 같은 문법만 받도록 여기서도 한 번 거른다.
            {"isnum",
             "_NUM_RE = re.compile(r\"[ \\t\\n\\r\\v\\f]*[+-]?(\\d+(\\.\\d*)?|\\.\\d+)([eE][+-]?\\d+)?[ \\t\\n\\r\\v\\f]*\\Z\")\n"
             "def _isnum(s):\n"
             "    return bool(_NUM_RE.match(s))\n"},
            {"num",
             "def _num(s):\n"
             "    if isinstance(s, (int, float)): return s\n"
             "    t = str(s)\n"
             "    if not _isnum(t):\n"
             "        raise Exception(\"숫자로 바꿀 수 없는 문자열: \\\"\" + t + \"\\\"\")\n"
             "    f = float(t)\n"
             "    return int(f) if f == int(f) else f\n"},
            {"input",
             "def _input(prompt=\"\"):\n"
             // 입력이 끊기면(파이프 끝, Ctrl+D) 파이썬은 EOFError 역추적을 쏟아낸다.
             // Venos 는 한 줄로 말하고 끝내므로 같은 문구로 맞춘다 — 세 백엔드가 같아야 한다.
             "    try:\n"
             "        s = input(prompt).strip()\n"
             "    except EOFError:\n"
             "        raise Exception(\"입력을 읽을 수 없습니다\")\n"
             "    if not _isnum(s):\n"
             "        return s\n"
             "    f = float(s)\n"
             "    return int(f) if f == int(f) else f\n"},
            // Venos 는 리스트 인덱스가 1부터다. int(k)-1 을 그대로 쓰면 0 이 파이썬의
             // 음수 인덱스가 되어 "에러" 가 "마지막 원소" 로 조용히 바뀐다.
             {"idx",
             "def _idx(c, k):\n"
             "    if isinstance(c, dict): return c[k]\n"
             "    i = int(k)\n"
             "    if i < 1 or i > len(c): raise Exception(\"리스트 범위를 벗어났습니다: \" + str(i))\n"
             "    return c[i - 1]\n"},
            {"k",
             "def _k(c, i):\n"
             "    return i if isinstance(c, dict) else int(i) - 1\n"},
            {"push",  "def _push(xs, v):\n    xs.append(v)\n    return xs\n"},
            // Venos 의 sort() 는 숫자만 있거나 문자열만 있는 리스트만 받는다. 파이썬은
            // 리스트끼리·딕셔너리끼리도 사전순으로 정렬해 버려서 **에러가 답으로 바뀐다**.
            // (숫자와 문자열이 섞인 건 파이썬도 TypeError 라 그쪽은 이미 같다.)
            {"sort",  "def _sort(xs):\n"
                      "    if not (all(isinstance(x, (int, float)) for x in xs)\n"
                      "            or all(isinstance(x, str) for x in xs)):\n"
                      "        raise Exception(\"sort() 는 숫자만 있거나 문자열만 있는 리스트만"
                      " 정렬할 수 있습니다\")\n"
                      "    xs.sort()\n"
                      "    return xs\n"},
            {"join",  "def _join(xs, sep):\n    return sep.join(_show(x) for x in xs)\n"},
            {"substr","def _substr(s, start, n):\n"
                       "    i = int(start) - 1\n"
                       "    if i < 0: raise Exception(\"substr() 의 시작 위치는 1부터입니다\")\n"
                       "    return s[i:i + int(n)]\n"},
            {"remove",
             "def _remove(c, k):\n"
             "    if isinstance(c, dict):\n"
             "        return int(c.pop(k, None) is not None)\n"
             "    return c.pop(int(k) - 1)\n"},
            {"round", "def _round(x, n=0):\n    p = 10 ** int(n)\n"
                      "    r = math.floor(x * p + 0.5) if x >= 0 else math.ceil(x * p - 0.5)\n"
                      "    return r if n == 0 else r / p\n"},
            {"reverse","def _reverse(x):\n    if isinstance(x, str): return x[::-1]\n"
                       "    x.reverse()\n    return x\n"},
            // 바꿀 문자열이 비면 파이썬은 글자 사이마다 끼워 넣는다. Venos 는 에러다.
            {"replace","def _replace(s, old, new):\n"
                       "    if old == \"\": raise Exception(\"replace() 의 바꿀 문자열은 비어 있을 수 없습니다\")\n"
                       "    return s.replace(old, new)\n"},
            // 빈 문자열을 찾으면 파이썬은 0 을 주지만 Venos 는 에러다
             {"find",  "def _find(a, b):\n"
                      "    if isinstance(a, str):\n"
                      "        if b == \"\": raise Exception(\"find() 로 찾을 문자열은 비어 있을 수 없습니다\")\n"
                      "        return a.find(b) + 1\n"
                      "    return a.index(b) + 1 if b in a else 0\n"},
            {"random","def _random(a, b):\n    a, b = int(a), int(b)\n    if a > b: a, b = b, a\n    return random.randint(a, b)\n"},
            {"rng",
             // step 을 안 쓴 for 는 Venos 가 실행할 때 방향을 정한다 (for i = 3 to n 에서
             // n 이 1 이면 내려간다). s=None 이 그 "방향은 그때 정함"을 뜻한다 —
             // 1 로 두면 내려가야 할 반복이 파이썬에서 조용히 한 번도 안 돈다.
             "def _rng(a, b, s=None):\n"
             "    if s is None: s = 1 if a <= b else -1\n"
             "    if a == int(a) and b == int(b) and s == int(s):\n"
             "        a, b, s = int(a), int(b), int(s)\n"
             "        return range(a, b + (1 if s > 0 else -1), s)\n"
             "    out, x = [], a\n"
             "    while (x <= b) if s > 0 else (x >= b):\n"
             "        out.append(x)\n"
             "        x += s\n"
             "    return out\n"},
            // 리스트는 사본을 준다 — Venos 는 루프 시작 시점의 리스트를 돌기 때문
            {"iter",  "def _iter(v):\n"
                      "    if isinstance(v, dict): return sorted(v)\n"
                      "    return list(v) if isinstance(v, list) else v\n"},
            {"error", "def _error(m):\n    raise Exception(m)\n"},
            {"exit",  "def _exit():\n    sys.exit(0)\n"},
            {"readfile",
             "def _readfile(p):\n    with open(p, encoding=\"utf-8\") as f:\n        return f.read()\n"},
            {"writefile",
             "def _writefile(p, s):\n"
             "    with open(p, \"w\", encoding=\"utf-8\") as f:\n        f.write(_show(s))\n    return 1\n"},
            {"appendfile",
             "def _appendfile(p, s):\n"
             "    with open(p, \"a\", encoding=\"utf-8\") as f:\n        f.write(_show(s))\n    return 1\n"},
        };
        // _q 는 _show 안에서만 쓰이고, join/writefile 등도 _show 에 기댄다
        std::set<string> want = helpers;
        if (want.count("join") || want.count("writefile") || want.count("appendfile")) want.insert("show");
        // _num 과 _input 은 _isnum 으로 "숫자처럼 보이는가"를 판정한다
        if (want.count("num") || want.count("input")) want.insert("isnum");
        string o;
        for (auto& h : want) {
            if (h == "show") { o += show; continue; }
            auto it = SRC.find(h);
            if (it != SRC.end()) o += it->second;
        }
        return o;
    }
};

// build 명령: .my → .cpp 변환 후 g++ 로 컴파일
static string currentFile;   // 현재 choose 된 파일 (셸 전역)

// 성공이면 true. main 은 이걸 그대로 종료 코드로 바꾼다 — 실패를 0 으로 알리면
// 채점 스크립트나 Makefile 이 죽은 프로그램을 성공으로 읽는다.
bool cmdBuild(const string& arg) {
    if (currentFile.empty()) { std::cout << "choose 로 파일을 먼저 선택하세요\n"; return false; }
    string fname = currentFile;

    string base = fname.substr(0, fname.size() - FILE_EXT.size());
    string cppName = base + ".cpp";
#ifdef _WIN32
    string exeName = base + ".exe";
    string runCmd  = base + ".exe";
#else
    string exeName = base;
    // 폴더가 붙어 있지 않을 때만 ./ 가 필요하다. 절대 경로 앞에 붙이면
    // ".//home/..." 이 되어 "not found" 로 죽는다 (venos build /경로/파일.my run).
    string runCmd  = (base.find('/') == string::npos) ? "./" + base : base;
#endif

    std::cout << "=== build: " << fname << " ===\n";
    string cppCode;
    try {
        auto tokens = lex(expandImports(fname));
        Parser parser(std::move(tokens));
        auto program = parser.parseProgram();
        CodeGen gen;
        cppCode = gen.generate(program);
    } catch (const LangError& e) {
        printError(e.what(), "!! codegen error: ");
        return false;
    }
    {
        std::ofstream out(toPath(cppName));
        out << cppCode;
    }
    std::cout << "C++ generated: " << cppName << "\n";
    std::cout << "compiling with g++...\n";
    // 윈도우는 정적 링크한다. 그러지 않으면 만들어진 exe 가 libstdc++-6.dll 등을 PATH 에서
    // 찾아야 해서 친구에게 건네면 안 열리고, PATH 에 다른 MinGW 의 libstdc++ 이 먼저
    // 걸리면 표준 라이브러리가 조용히 오동작한다 (파일 열기가 늘 성공하는 걸 본 적 있다).
    const string FLAGS = string("g++ -std=c++17 -O2")
#ifdef _WIN32
                       + " -static"
#endif
                       ;
    int rc;
#ifdef _WIN32
    // 한글 경로면 g++ 를 그냥 부를 수 없다. venos 자신은 _wfopen 으로 열지만 **g++ 의 argv 는
    // ANSI 코드페이지로 변환돼 들어가서** "No such file or directory" 로 죽는다 (윈도우 CI 가 잡음).
    // 우리가 g++ 를 고칠 수는 없으니, 그 폴더로 잠깐 들어가 ASCII 이름으로만 부르고
    // 결과를 제자리에 돌려놓는다. 학생이 받는 .cpp/.exe 이름은 그대로다.
    if (hasNonAscii(cppName) || hasNonAscii(exeName)) {
        const string TMP_CPP = "venos_build_tmp.cpp";
        const string TMP_EXE = "venos_build_tmp.exe";
        string dir = dirOf(cppName);
        std::error_code ec;
        fs::path prev = fs::current_path(ec);
        if (!ec && !dir.empty()) fs::current_path(toPath(dir), ec);
        if (ec) {
            std::cout << "!! 폴더로 들어갈 수 없습니다: " << (dir.empty() ? "." : dir) << "\n";
            return false;
        }
        // 이제 CWD 가 그 폴더이므로 파일 이름만 쓴다 (폴더 이름에도 한글이 있을 수 있다)
        string exeHere = dir.empty() ? exeName : exeName.substr(dir.size() + 1);
        { std::ofstream out(TMP_CPP, std::ios::binary); out << cppCode; }
        rc = runShell(FLAGS + " -o " + TMP_EXE + " " + TMP_CPP);
        if (rc == 0) {
            fs::remove(toPath(exeHere), ec);
            fs::rename(fs::path(TMP_EXE), toPath(exeHere), ec);
            if (ec) { std::cout << "!! 만든 실행 파일을 제자리로 옮기지 못했습니다\n"; rc = -1; }
        }
        std::error_code ec2;
        // 학생 파일이 하필 venos_build_tmp.my 라면 임시 이름이 결과물 이름과 같아진다 —
        // 그때 지우면 방금 만든 것을 지우는 꼴이다.
        string cppHere = dir.empty() ? cppName : cppName.substr(dir.size() + 1);
        if (cppHere != TMP_CPP) fs::remove(fs::path(TMP_CPP), ec2);
        if (exeHere != TMP_EXE) fs::remove(fs::path(TMP_EXE), ec2);
        if (!prev.empty()) fs::current_path(prev, ec2);
    } else
#endif
    {
        rc = runShell(FLAGS + " -o \"" + exeName + "\" \"" + cppName + "\"");
    }
    if (rc != 0) {
        std::cout << "!! g++ failed (is g++ installed?)\n";
        std::cout << "   the generated C++ is still there, you can compile it yourself: " << cppName << "\n";
        return false;
    }
    std::cout << "build OK: " << exeName << "  (run: " << runCmd << ")\n";
    if (arg == "run") {
        std::cout << "----- run -----\n" << std::flush;
        // 경로에 공백이 있으면 셸이 두 낱말로 읽는다 ("내 과제/정렬.my")
        int rrc = runShell("\"" + runCmd + "\"");
        if (rrc != 0) {
            // system() 이 주는 건 종료 코드가 아니라 wait 상태다 — 1 로 끝난 프로그램이
            // 256 으로 찍히고 있었다. POSIX 에서는 위쪽 바이트를 벗겨야 한다.
#ifndef _WIN32
            if ((rrc & 0x7F) == 0) rrc = (rrc >> 8) & 0xFF;
#endif
            std::cout << "(program exited with code " << rrc << ")\n";
            return false;
        }
    }
    return true;
}

// topython 명령: .my → 읽을 수 있는 .py
// build 와 달리 컴파일하지 않는다 — 학생이 읽고 다음 언어로 넘어가라고 내주는 파일이다.
bool cmdTopython() {
    if (currentFile.empty()) { std::cout << "choose 로 파일을 먼저 선택하세요\n"; return false; }
    string fname = currentFile;
    string pyName = fname.substr(0, fname.size() - FILE_EXT.size()) + ".py";

    string pyCode;
    try {
        auto tokens = lex(expandImports(fname));
        Parser parser(std::move(tokens));
        auto program = parser.parseProgram();
        PyGen gen;
        pyCode = gen.generate(program);
    } catch (const LangError& e) {
        printError(e.what(), "!! python conversion failed: ");
        return false;
    }
    {
        std::ofstream out(toPath(pyName));
        out << pyCode;
    }
    std::cout << "Python generated: " << pyName << "  (run: python3 " << pyName << ")\n";
    return true;
}


// import 문 존재 검사 — 웹/REPL 은 파일 병합(expandImports)을 거치지 않아
// 그대로 파싱하면 엉뚱한 문법 에러가 나므로, 미리 잡아 친절하게 알려준다
static bool containsImport(const string& src) {
    std::istringstream is(src);
    string ln;
    while (std::getline(is, ln)) {
        string t = trim(ln);
        if (t.rfind("import", 0) == 0) {
            string rest = t.substr(6);
            if (rest.empty() || rest[0] == ' ' || rest[0] == '\t' || rest[0] == '"')
                return true;
        }
    }
    return false;
}

#ifdef VENOS_WASM
// ============================================================
//  WASM 진입점 — 웹 플레이그라운드에서 호출
// ============================================================
extern "C" EMSCRIPTEN_KEEPALIVE void venos_run(const char* code) {
    string src(code);
    // 에러 줄 표시용 소스 보관
    g_lineMap.clear();
    g_srcLines.clear();
    string cur;
    for (char c : src) {
        if (c == '\n') { g_srcLines.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (containsImport(src)) {
        std::cout << "!! 에러: 웹 플레이그라운드에서는 import 를 지원하지 않습니다 (데스크톱 전용)\n";
        std::cout << std::flush;
        return;
    }
    g_stopped = false;
    g_pumpTick = 0;
    g_pumpLast = emscripten_get_now();   // 시작 직후부터 양보하지 않도록 시계를 맞춰 둔다
    try {
        runSource(src);
        std::cout << (g_stopped ? "=== 중단했습니다 / stopped ===\n" : "=== done ===\n");
    } catch (const LangError& e) {
        printError(e);
    } catch (const std::exception& e) {
        std::cout << "!! 내부 에러: " << e.what() << "\n";
    }
    std::cout << std::flush;
}

// 플레이그라운드의 "Python 으로 보기" — 변환한 파이썬 소스를 그대로 출력으로 흘려보낸다.
// (venos_run 과 같은 경로라 웹 쪽에 새 배관이 필요 없다.)
extern "C" EMSCRIPTEN_KEEPALIVE void venos_topython(const char* code) {
    string src(code);
    g_lineMap.clear();
    g_srcLines.clear();
    string cur;
    for (char c : src) {
        if (c == '\n') { g_srcLines.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (containsImport(src)) {
        std::cout << "!! 에러: 웹 플레이그라운드에서는 import 를 지원하지 않습니다 (데스크톱 전용)\n";
        std::cout << std::flush;
        return;
    }
    try {
        auto tokens = lex(src);
        Parser parser(std::move(tokens));
        auto program = parser.parseProgram();
        PyGen gen;
        std::cout << gen.generate(program);
    } catch (const LangError& e) {
        printError(e.what(), "!! python conversion failed: ");
    } catch (const std::exception& e) {
        std::cout << "!! 내부 에러: " << e.what() << "\n";
    }
    std::cout << std::flush;
}
// input 프롬프트는 개행이 없는 "부분 줄"이라 emscripten 의 stdout 버퍼에 남아 있다가
// 다음 개행에 딸려 나온다. 정상 흐름에선 readLine 이 입력값을 되찍으며 바로 해소되지만,
// 그 전에 예외가 튀면 찌꺼기가 남아 **다음 실행의 첫 줄에 붙는다**.
// 플레이그라운드가 새 실행을 시작하기 전에 이걸 불러 버퍼를 비운다.
extern "C" EMSCRIPTEN_KEEPALIVE void venos_flush() {
    std::cout << "\n" << std::flush;
}
#else   // ---- 이하 네이티브 전용 (CLI 셸) ----

// 중괄호 열림/닫힘 차이 (문자열/주석 무시) — REPL 여러 줄 입력 판단용
static int braceDelta(const string& s) {
    int d = 0;
    bool inStr = false;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (inStr) {
            if (c == '\\') { i++; continue; }
            if (c == '"') inStr = false;
            continue;
        }
        if (c == '"') { inStr = true; continue; }
        if (c == '#') break;
        if (c == '{') d++;
        if (c == '}') d--;
    }
    return d;
}

// REPL — 한 줄씩 즉시 실행, 변수/함수/클래스는 세션 동안 유지
void cmdRepl() {
    clearScreen();
    std::cout << "=== Venos REPL ===  (:q / quit / 나가기 로 종료)\n";
    std::cout << "한 줄씩 바로 실행됩니다. 값만 입력하면 결과를 출력해요 (예: 3 * 7)\n\n";
    Env env;
    g_funcs.clear();
    g_classes.clear();
    g_callDepth = 0;
    g_frames.clear();   // 앞 실행이 스택 넘침 등으로 중간에 끊겼으면 프레임이 남아 있다
    g_lineMap.clear();
    g_srcLines.clear();
    g_global = &env;
    std::vector<StmtP> keepAlive;   // 함수/클래스 AST 소유권 유지용
    string line;
    while (true) {
        std::cout << ">> " << std::flush;
        if (!readLine(line)) break;
        string t = trim(line);
        // 나가는 말은 여러 가지로 받아 준다 — 못 나가서 창을 닫는 학생이 없도록
        if (t == ":q" || t == "q" || t == "exit" || t == "quit" || t == "나가기") break;
        if (t.empty()) continue;

        // 블록이 열려 있으면 닫힐 때까지 이어서 입력
        string src = line;
        int depth = braceDelta(line);
        while (depth > 0) {
            std::cout << ".. " << std::flush;
            string more;
            if (!readLine(more)) { depth = 0; break; }
            src += "\n" + more;
            depth += braceDelta(more);
        }

        if (containsImport(src)) {
            std::cout << "!! 에러: REPL 에서는 import 를 지원하지 않습니다 (파일 실행에서만 가능)\n";
            continue;
        }

        // 1차: 그대로 파싱. 실패하면 "print (입력)" 으로 재시도 → 값 입력 시 자동 출력
        std::vector<StmtP> prog;
        try {
            Parser ps(lex(src));
            prog = ps.parseProgram();
        } catch (LangError& first) {
            try {
                Parser ps2(lex("print " + src));
                prog = ps2.parseProgram();
            } catch (...) {
                printError(first.what());
                continue;
            }
        }
        // 실행 (재귀 대비 큰 스택에서)
        try {
            runOnBigStack([&] {
                for (auto& s : prog) {
                    if (auto* fn = dynamic_cast<FuncStmt*>(s.get())) g_funcs[fn->name] = fn;
                    if (auto* cs = dynamic_cast<ClassStmt*>(s.get())) g_classes[cs->name] = cs;
                }
                // 단독 함수 호출이면 반환값을 보여줌 (0 = return 없음이므로 생략)
                if (prog.size() == 1) {
                    if (auto* es = dynamic_cast<ExprStmt*>(prog[0].get())) {
                        Value v = es->e->eval(env);
                        if (!(v.kind == Value::NUM && v.num == 0))
                            std::cout << v.toString() << "\n";
                        return;
                    }
                }
                for (auto& s : prog) {
                    Flow f = s->exec(env);
                    if (f == Flow::NORMAL) continue;
                    throw LangError(f == Flow::BREAK    ? KW_BREAK + " 는 반복문 안에서만 쓸 수 있습니다"
                                  : f == Flow::CONTINUE ? KW_CONTINUE + " 는 반복문 안에서만 쓸 수 있습니다"
                                                        : KW_RETURN + " 은 함수 안에서만 쓸 수 있습니다");
                }
            });
        } catch (ExitSignal&) {
            break;
        } catch (LangError& e) {
            printError(e);
        } catch (std::exception& e) {
            std::cout << "!! 내부 에러: " << e.what() << "\n";
        }
        for (auto& s : prog) keepAlive.push_back(std::move(s));
    }
    g_global = nullptr;
    std::cout << "(REPL 종료)\n";
}

// ============================================================
//  6. CLI 셸
// ============================================================

string withExt(string name) {
    if (name.size() < FILE_EXT.size()
        || name.substr(name.size() - FILE_EXT.size()) != FILE_EXT)
        name += FILE_EXT;
    return name;
}

static std::vector<string> myFiles() {
    std::vector<string> out;
    for (auto& entry : fs::directory_iterator(fs::current_path())) {
        // u8string()은 C++17에선 string, C++20에선 u8string(char8_t)을
        // 반환하므로 바이트 단위 복사로 양쪽 표준 모두 호환되게 처리
        auto u8 = entry.path().filename().u8string();
        string name(u8.begin(), u8.end());
        if (name.size() >= FILE_EXT.size()
            && name.substr(name.size() - FILE_EXT.size()) == FILE_EXT)
            out.push_back(name);
    }
    return out;
}

void cmdCreate(const string& name) {
    if (name.empty()) { std::cout << "사용법: create <파일이름>\n"; return; }
    string fname = withExt(name);
    if (fs::exists(toPath(fname))) { std::cout << "이미 존재하는 파일: " << fname << "\n"; return; }
    std::ofstream(toPath(fname)).close();
    currentFile = fname;
    std::cout << "생성됨: " << fname << " (자동으로 choose 됨)\n";
}

void cmdChoose(const string& name) {
    if (name.empty()) { std::cout << "사용법: choose <파일이름>\n"; return; }
    string fname = withExt(name);
    if (!fs::exists(toPath(fname))) {
        std::cout << "파일 없음: " << fname << "\n";
        auto files = myFiles();
        if (!files.empty()) {
            std::cout << "현재 있는 파일:\n";
            for (auto& f : files) std::cout << "  - " << f << "\n";
        }
        return;
    }
    currentFile = fname;
    std::cout << "선택됨: " << fname << "\n";
}

void cmdShow() {
    if (currentFile.empty()) { std::cout << "choose 로 파일을 먼저 선택하세요\n"; return; }
    std::ifstream in(toPath(currentFile));
    std::vector<string> lines;
    string line;
    while (std::getline(in, line)) lines.push_back(line);
    scrollViewer(lines, currentFile);   // 방향키로 스크롤, q 로 나가기
}

void cmdCode() {
    if (currentFile.empty()) { std::cout << "choose 로 파일을 먼저 선택하세요\n"; return; }
    std::vector<string> lines;
    {
        std::ifstream in(toPath(currentFile));
        string l;
        while (std::getline(in, l)) lines.push_back(l);
    }

    string notice;
    string input;
    while (true) {
        clearScreen();
        const size_t WIN = 15;   // 편집 중엔 마지막 15줄만 (전체는 :v)
        std::cout << "── " << currentFile << " (" << lines.size()
                  << "줄) │ :v 전체보기 :q 저장 :run 실행 :paste :line N :d :c ──\n";
        size_t start = lines.size() > WIN ? lines.size() - WIN : 0;
        if (start > 0)
            std::cout << "  … (위 " << start << "줄은 :v 로 스크롤) …\n";
        if (lines.empty()) std::cout << "  (빈 파일)\n";
        for (size_t n = start; n < lines.size(); n++)
            std::cout << "  " << (n + 1) << " | " << lines[n] << "\n";
        if (!notice.empty()) { std::cout << notice << "\n"; notice.clear(); }

        std::cout << "  " << (lines.size() + 1) << " > " << std::flush;
        if (!readLine(input)) break;
        string cmd = trim(input);

        if (cmd == ":q") break;
        if (cmd == ":run") {
            {   // 저장 후 실행
                std::ofstream out(toPath(currentFile));
                for (auto& l : lines) out << l << "\n";
            }
            clearScreen();
            drawBanner();
            std::cout << "----- 실행할 코드: " << currentFile << " -----\n";
            for (size_t n = 0; n < lines.size(); n++)
                std::cout << "  " << (n + 1) << " | " << lines[n] << "\n";
            std::cout << "\n----- 실행 결과 -----\n";
            try {
                runSourceBigStack(expandImports(currentFile));   // 저장본 기준 (import 지원)
                std::cout << "=== done ===\n";
            } catch (const LangError& e) {
                printError(e);
            } catch (const std::exception& e) {
                std::cout << "!! 내부 에러: " << e.what() << "\n";
            }
            std::cout << "\n(엔터를 누르면 에디터로 돌아갑니다) " << std::flush;
            string dummy;
            readLine(dummy);
            continue;
        }
        if (cmd == ":v") {       // 방향키 스크롤 뷰어
            scrollViewer(lines, currentFile);
            continue;
        }
        if (cmd == ":paste") {   // 여러 줄 한 번에 붙여넣기 (:end 로 종료)
            std::cout << "  (붙여넣기 모드 — 코드를 붙여넣고 마지막 줄에 :end 입력)\n";
            string pl;
            int added = 0;
            while (readLine(pl)) {
                if (trim(pl) == ":end") break;
                lines.push_back(pl);
                added++;
            }
            notice = "(" + std::to_string(added) + "줄 추가됨)";
            continue;
        }
        if (cmd == ":d") {
            if (!lines.empty()) { lines.pop_back(); notice = "(마지막 줄 삭제됨)"; }
            else notice = "(삭제할 줄이 없음)";
            continue;
        }
        if (cmd == ":c") { lines.clear(); notice = "(전체 삭제됨)"; continue; }
        if (cmd.rfind(":line", 0) == 0) {
            int n = 0;
            try { n = std::stoi(trim(cmd.substr(5))); } catch (...) {}
            if (n < 1 || n > (int)lines.size()) {
                notice = "(줄 번호가 잘못됨: 1 ~ " + std::to_string(lines.size()) + ")";
                continue;
            }
            std::cout << "  기존 " << n << " | " << lines[n - 1] << "\n";
            std::cout << "  수정 " << n << " > " << std::flush;
            string newLine;
            if (readLine(newLine)) {
                lines[n - 1] = newLine;
                notice = "(" + std::to_string(n) + "번 줄 수정됨)";
            }
            continue;
        }
        lines.push_back(input);
    }

    std::ofstream out(toPath(currentFile));
    for (auto& l : lines) out << l << "\n";
    clearScreen();
    std::cout << "저장됨: " << currentFile << " (" << lines.size() << "줄)\n";
}

bool cmdRun() {
    if (currentFile.empty()) { std::cout << "choose 로 파일을 먼저 선택하세요\n"; return false; }
    std::cout << "=== running: " << currentFile << " ===\n";
    try {
        runSourceBigStack(expandImports(currentFile));
        std::cout << "=== done ===\n";
    } catch (const LangError& e) {
        printError(e);
        return false;
    } catch (const std::exception& e) {
        std::cout << "!! 내부 에러: " << e.what() << "\n";
        return false;
    }
    return true;
}

void cmdList() {
    auto files = myFiles();
    if (files.empty()) { std::cout << "  (" << FILE_EXT << " 파일 없음)\n"; return; }
    for (auto& name : files)
        std::cout << "  " << name << (name == currentFile ? "   <- 현재 선택" : "") << "\n";
}

void cmdHelp() {
    std::cout <<
        "명령어:\n"
        "  create <이름>  새 파일 생성\n"
        "  choose <이름>  파일 선택\n"
        "  code          코딩 모드 (:q 나가기, :run 바로 실행)\n"
        "  show          파일 내용 보기\n"
        "  run           실행 (인터프리터)\n"
        "  build         진짜 실행 파일로 컴파일 (.my → .cpp → exe)\n"
        "  build run     컴파일 후 바로 실행\n"
        "  topython      같은 프로그램의 파이썬 버전을 만든다 (.my → .py)\n"
        "  list          파일 목록\n"
        "  repl          한 줄씩 즉시 실행 모드\n"
        "  clear         화면 지우기\n"
        "  exit          종료\n"
        "\n언어 문법 예시:\n"
        "  let x = 10       x += 1       let name = input \"이름: \"\n"
        "  print \"x =\", x, \"끝\"          # print 는 , 로 여러 값\n"
        "  print \"1줄\\n2줄\"              # \\n \\t \\\" \\\\ 이스케이프\n"
        "  if x > 5 then { ... } else if x > 0 { ... } else { ... }\n"
        "  while x > 0 do { x -= 1  if x == 3 { break } }\n"
        "  for i = 1 to 10 step 2 { print i }\n"
        "  func add(a, b) { return a + b }     print add(3, 4)\n"
        "  class 사람 { func init(이름) { self.이름 = 이름 }\n"
        "              func 인사() { print self.이름 } }\n"
        "  let p = 사람(\"미르\")   p.인사()   p.나이 = 15   p.나이 += 1\n"
        "  for ch in \"안녕\" { print ch }      for x in xs { print x }\n"
        "  let xs = [10, 20, 30]   print xs[1]   xs[2] += 5   print \"코딩\"[1]\n"
        "  let d = {\"이름\": \"미르\"}   d[\"나이\"] = 15   print d[\"이름\"]\n"
        "  딕셔너리: keys(d) has(d,키) remove(d,키) len(d)  for k in d { }\n"
        "  리스트: push(xs,v) pop(xs) sort(xs) len(xs)\n"
        "  수학: random(1,6) round floor ceil abs sqrt min max\n"
        "  변환: num(\"15\") str(3)      true/false = 1/0\n"
        "  문자열: split join upper lower find replace substr\n"
        "  기타: readfile writefile appendfile exists(경로) time() exit()\n"
        "  import \"utils.my\"   try { } catch 오류 { }   error(\"메시지\")   copy(값)\n"
        "  CLI: venos 파일.my (바로 실행) / venos build 파일.my run\n";
}

#ifdef _WIN32
// 윈도우는 main 의 argv 를 시스템 ANSI 코드페이지로 준다. 한글 파일 이름이 물음표로
// 뭉개져서 "파일 없음: ??_??.my" 가 되고, 한국어 사용자용 언어인데 정렬.my 를 못 연다.
// 명령줄을 UTF-16 으로 다시 받아 UTF-8 로 바꿔 끼운다 (프로그램 내부는 전부 UTF-8).
static std::vector<string> g_wideArgs;
static std::vector<char*>  g_wideArgv;
static void useUtf8Argv(int& argc, char**& argv) {
    int n = 0;
    LPWSTR* w = CommandLineToArgvW(GetCommandLineW(), &n);
    if (!w || n <= 0) return;
    g_wideArgs.clear();
    for (int i = 0; i < n; i++) {
        int need = WideCharToMultiByte(CP_UTF8, 0, w[i], -1, nullptr, 0, nullptr, nullptr);
        string u8(need > 1 ? need - 1 : 0, '\0');
        if (need > 1) WideCharToMultiByte(CP_UTF8, 0, w[i], -1, &u8[0], need, nullptr, nullptr);
        g_wideArgs.push_back(std::move(u8));
    }
    LocalFree(w);
    g_wideArgv.clear();
    for (auto& a : g_wideArgs) g_wideArgv.push_back(a.empty() ? const_cast<char*>("") : &a[0]);
    g_wideArgv.push_back(nullptr);
    argc = n;
    argv = g_wideArgv.data();
}
#endif

int main(int argc, char** argv) {
#ifdef _WIN32
    useUtf8Argv(argc, argv);
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD outMode = 0;
    if (GetConsoleMode(hOut, &outMode))
        SetConsoleMode(hOut, outMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif
    // ---- CLI 모드: 셸 없이 파일 바로 실행/빌드 ----
    //   venos 파일.my            실행
    //   venos run 파일.my        실행
    //   venos build 파일.my      빌드
    //   venos build 파일.my run  빌드 후 실행
    if (argc >= 2) {
        string a1 = argv[1];
        // 다른 도구들이 다 받는 것들 — 이게 없으면 `venos --help` 가 "파일 없음: --help.my" 다
        if (a1 == "--version" || a1 == "-v" || a1 == "version") {
            std::cout << "Venos " << VENOS_VERSION << "\n";
            return 0;
        }
        if (a1 == "--help" || a1 == "-h" || a1 == "help") {
            std::cout <<
                "Venos " << VENOS_VERSION << " — 실행되는 의사코드, 파이썬으로 나가는 다리\n"
                "\n"
                "사용법:\n"
                "  venos 파일.my              바로 실행 (인터프리터)\n"
                "  venos run 파일.my          위와 같음\n"
                "  venos build 파일.my        C++ 로 옮겨 g++ 로 컴파일 → 실행 파일\n"
                "  venos build 파일.my run    빌드한 뒤 바로 실행\n"
                "  venos topython 파일.my     같은 프로그램의 파이썬 버전을 만든다 (.my → .py)\n"
                "  venos                      대화형 셸 (그 안에서 help 로 명령 목록)\n"
                "  venos --version            버전\n"
                "\n"
                "확장자 .my 는 생략해도 된다 (venos 정렬 → 정렬.my).\n"
                "설치 없이 써 보려면: https://vpdrla.github.io/Venos/\n"
                "언어 명세: VENOS_SPEC.md\n";
            return 0;
        }
        // 남는 인자를 조용히 버리면 "venos topython 정렬.my -o 결과.py" 가 아무 말 없이
        // 엉뚱한 곳에 파일을 쓴다. 모르는 인자는 쓰는 법과 함께 거절한다.
        auto extra = [&](int used, const char* usage) {
            if (argc <= used) return false;
            std::cout << "모르는 인자: " << argv[used] << "\n   쓰는 법: " << usage << "\n";
            return true;
        };
        if (a1 == "topython" && argc >= 3) {
            if (extra(3, "venos topython 파일.my")) return 1;
            string f = withExt(argv[2]);
            if (!fs::exists(toPath(f))) { std::cout << "파일 없음: " << f << "\n"; return 1; }
            currentFile = f;
            return cmdTopython() ? 0 : 1;
        }
        if (a1 == "build" && argc >= 3) {
            bool wantRun = (argc >= 4 && string(argv[3]) == "run");
            if (extra(wantRun ? 4 : 3, "venos build 파일.my [run]")) return 1;
            string f = withExt(argv[2]);
            if (!fs::exists(toPath(f))) { std::cout << "파일 없음: " << f << "\n"; return 1; }
            currentFile = f;
            return cmdBuild(wantRun ? "run" : "") ? 0 : 1;
        }
        bool viaRun = (a1 == "run" && argc >= 3);
        if (extra(viaRun ? 3 : 2, "venos 파일.my   (또는 venos run 파일.my)")) return 1;
        string f = withExt(viaRun ? argv[2] : a1);
        if (!fs::exists(toPath(f))) { std::cout << "파일 없음: " << f << "\n"; return 1; }
        currentFile = f;
        return cmdRun() ? 0 : 1;
    }
    clearScreen();
    drawBanner();
    string line;
    while (true) {
        std::cout << "\n" << (currentFile.empty() ? "venos" : "venos [" + currentFile + "]") << " $ " << std::flush;
        if (!readLine(line)) break;

        std::istringstream iss(line);
        string cmd, arg;
        iss >> cmd;
        std::getline(iss, arg);
        arg = trim(arg);

        if      (cmd.empty())      continue;
        else if (cmd == "create")  cmdCreate(arg);
        else if (cmd == "choose")  cmdChoose(arg);
        else if (cmd == "code")    cmdCode();
        else if (cmd == "show")    cmdShow();
        else if (cmd == "run")     cmdRun();
        else if (cmd == "list")    cmdList();
        else if (cmd == "build")   cmdBuild(arg);
        else if (cmd == "topython") cmdTopython();
        else if (cmd == "repl")    cmdRepl();
        else if (cmd == "clear")   { clearScreen(); drawBanner(); }
        else if (cmd == "help")    cmdHelp();
        else if (cmd == "exit" || cmd == "quit") break;
        else {
            // 셸에 처음 온 사람이 가장 먼저 하는 일은 코드를 치는 것이다.
            // "알 수 없는 명령어: let" 만 보여 주면 repl 이 있다는 걸 알 길이 없다.
            static const std::vector<string> CMDS = {
                "create", "choose", "code", "show", "run", "list",
                "build", "topython", "repl", "clear", "help", "exit",
            };
            bool looksLikeCode =
                cmd == KW_LET || cmd == KW_PRINT || cmd == KW_IF || cmd == KW_WHILE
             || cmd == KW_FOR || cmd == KW_FUNC || cmd == KW_CLASS || cmd == KW_TRY
             || cmd == KW_RETURN || cmd == "import"
             || line.find('=') != string::npos || line.find('(') != string::npos;
            std::cout << "알 수 없는 명령어: " << cmd
                      << (looksLikeCode
                            ? "  (코드를 한 줄씩 실행하려면 repl 을 먼저 치세요)"
                            : suggestName(cmd, CMDS))
                      << "\n   명령어 목록은 help\n";
        }
    }
    std::cout << "종료합니다.\n";
    return 0;
}
#endif  // VENOS_WASM 아님 (네이티브 셸 끝)
