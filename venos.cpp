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

static const int MAX_RECURSION = 2000;   // 함수 재귀 깊이 제한

// ============================================================
//  플랫폼 헬퍼 — Windows 한글 입력/파일명 깨짐 방지
// ============================================================
static bool readLine(string& out) {
#ifdef VENOS_WASM
    char* r = js_prompt_raw(g_pendingPrompt.c_str());
    out = r;
    free(r);
    g_pendingPrompt.clear();
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

// \033[2J: 보이는 화면 지우기, \033[3J: 스크롤백(위로 올린 기록)까지 지우기, \033[H: 커서 맨 위로
static void clearScreen() { std::cout << "\033[2J\033[3J\033[H" << std::flush; }

static void drawBanner() {
    std::cout << "==========================================\n";
    std::cout << "  Venos Shell v0.6.0  (help 로 도움말)\n";
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

struct LangError : std::runtime_error {
    LangError(const string& msg) : std::runtime_error(msg) {}
};

// import 로 파일이 병합되면, 병합된 줄번호 → "원본파일 줄 N" 매핑을 채운다.
// 비어 있으면(단일 파일) 그냥 "줄 N" 으로 표시.
static std::vector<string> g_lineMap;
static string lineTag(int line) {
    if (line >= 1 && line < (int)g_lineMap.size() && !g_lineMap[line].empty())
        return "[" + g_lineMap[line] + "] ";
    return "[줄 " + std::to_string(line) + "] ";
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
    if (merged >= 1 && merged <= (int)g_srcLines.size())
        std::cout << "    " << tag << " | " << trim(g_srcLines[merged - 1]) << "\n";
}

// ============================================================
//  import 전개 — 실행/빌드 전에 import "파일.my" 줄을 해당 파일
//  내용으로 치환하고, 병합 줄번호 → 원본 위치 매핑을 만든다.
//  같은 파일은 한 번만 로드 (중복/순환 import 자동 방지).
// ============================================================
static fs::path importToPath(const string& utf8) { return toPath(utf8); }
static void loadWithImports(const string& rawPath, std::set<string>& loaded,
                            string& out, std::vector<string>& lmap,
                            bool& sawImport, const string& fromWhere) {
    string path = rawPath;
    if (path.size() < FILE_EXT.size()
        || path.substr(path.size() - FILE_EXT.size()) != FILE_EXT)
        path += FILE_EXT;
    if (loaded.count(path)) return;      // 이미 로드됨 → 스킵
    loaded.insert(path);

    std::ifstream in(importToPath(path));
    if (!in)
        throw LangError("import 실패: 파일을 열 수 없습니다: " + path
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
                        loadWithImports(t.substr(p + 1, q - p - 1), loaded, out, lmap,
                                        sawImport, path + " 줄 " + std::to_string(no));
                        continue;    // import 줄 자체는 출력에 넣지 않음
                    }
                }
            }
        }
        out += line;
        out += "\n";
        lmap.push_back(path + " 줄 " + std::to_string(no));
    }
}

static string expandImports(const string& mainPath) {
    std::set<string> loaded;
    string out;
    std::vector<string> lmap;
    lmap.push_back("");                  // 줄번호는 1부터라 0번은 비움
    bool sawImport = false;
    loadWithImports(mainPath, loaded, out, lmap, sawImport, "");
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
};

// 제어 흐름 시그널 — break/continue/return 을 예외로 전달
struct BreakSignal {};
struct ContinueSignal {};
struct ReturnSignal { Value v; };
struct ExitSignal {};   // exit() — 프로그램 정상 종료

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
    virtual void exec(Env& env) = 0;
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
        if (!v) throw LangError(lineTag(line) + "정의되지 않은 변수: " + name);
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
            throw LangError(lineTag(line) + "" + t.kindName() + "에는 [ ] 를 쓸 수 없습니다");
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
// 이항 연산의 실제 처리 — BinExpr 와 원소 복합 대입(xs[i] += ...)이 공유
static Value applyBin(Tok op, const Value& a, const Value& b, int line) {
    auto err = [&](const string& m) {
        return LangError(lineTag(line) + "" + m);
    };
    if (op == Tok::PLUS && (a.kind == Value::STR || b.kind == Value::STR))
        return Value::text(a.toString() + b.toString());
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
        if (a.kind != b.kind) throw err("숫자와 문자열은 비교할 수 없습니다");
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
        throw err(a.kindName() + "와(과) " + b.kindName() + "는 이 연산이 안 됩니다");
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
        if (it == t.map->end())
            throw LangError(lineTag(line) + "필드가 없습니다: ." + field);
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
        try {
            size_t used = 0;
            double d = std::stod(line, &used);
            if (used == line.size()) return Value::number(d);
        } catch (...) {}
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
    void exec(Env& env) override { env.define(name, val->eval(env)); }
};
struct AssignStmt : Stmt {
    string name; ExprP val; int line;
    AssignStmt(string n, ExprP v, int l) : name(std::move(n)), val(std::move(v)), line(l) {}
    void exec(Env& env) override {
        Value* slot = env.find(name);
        if (!slot)
            throw LangError(lineTag(line) + "선언되지 않은 변수에 대입: " + name
                            + "  (" + KW_LET + " " + name + " = ... 로 먼저 선언하세요)");
        *slot = val->eval(env);
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
        if (it == cur->map->end()) throw err("필드가 없습니다: ." + a.field);
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
    throw err(cur->kindName() + "에는 [ ] 를 쓸 수 없습니다");
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
    throw err(cur->kindName() + "에는 [ ] 를 쓸 수 없습니다");
}

// 경로 대입: x[1] = v,  obj.필드 = v,  obj.점수[2] = v ...
struct PathAssignStmt : Stmt {
    string name; std::vector<Accessor> path; ExprP val; int line = 0;
    void exec(Env& env) override {
        Value* cur = env.find(name);
        if (!cur)
            throw LangError(lineTag(line) + "정의되지 않은 변수: " + name);
        for (size_t k = 0; k + 1 < path.size(); k++)
            cur = stepIntoAcc(cur, path[k], env);
        cur = putSlot(cur, path.back(), env);
        *cur = val->eval(env);
    }
};
// 경로 복합 대입: xs[i] += 1,  obj.나이 += 1  (기존 값이 있어야 함)
struct PathCompoundStmt : Stmt {
    string name; std::vector<Accessor> path; Tok op; ExprP rhs; int line = 0;
    void exec(Env& env) override {
        Value* cur = env.find(name);
        if (!cur)
            throw LangError(lineTag(line) + "정의되지 않은 변수: " + name);
        for (auto& a : path)
            cur = stepIntoAcc(cur, a, env);
        *cur = applyBin(op, *cur, rhs->eval(env), line);
    }
};
struct PrintStmt : Stmt {
    std::vector<ExprP> vals;   // print a, b, c → 공백으로 이어서 출력
    void exec(Env& env) override {
        string out;
        for (size_t i = 0; i < vals.size(); i++) {
            if (i) out += " ";
            out += vals[i]->eval(env).toString();
        }
        std::cout << out << "\n";
    }
};
struct ExprStmt : Stmt {
    ExprP e;
    ExprStmt(ExprP e) : e(std::move(e)) {}
    void exec(Env& env) override { e->eval(env); }
};
struct BlockStmt : Stmt {
    std::vector<StmtP> stmts;
    void exec(Env& env) override {
        for (auto& s : stmts) s->exec(env);
    }
};
struct IfStmt : Stmt {
    ExprP cond; StmtP thenB, elseB;   // elseB 는 블록이거나 또 다른 IfStmt (else if 체인)
    void exec(Env& env) override {
        if (cond->eval(env).truthy()) thenB->exec(env);
        else if (elseB) elseB->exec(env);
    }
};
// try { ... } catch 오류 { ... } — 런타임 에러를 잡아 메시지를 변수에 담음.
// break/continue/return/exit 는 에러가 아니므로 그대로 통과한다.
struct TryStmt : Stmt {
    StmtP tryB, catchB;
    string var;
    void exec(Env& env) override {
        try {
            tryB->exec(env);
        } catch (LangError& e) {
            env.vars[var] = Value::text(e.what());   // 현재 스코프의 지역 변수로
            catchB->exec(env);
        }
    }
};

struct WhileStmt : Stmt {
    ExprP cond; StmtP body;
    void exec(Env& env) override {
#ifdef VENOS_WASM
        // 웹에선 무한 루프가 탭을 얼리므로 상한 유지. 네이티브는 상한 없음 (빌드본과 동작 일치)
        long long guard = 0;
#endif
        while (cond->eval(env).truthy()) {
            try { body->exec(env); }
            catch (ContinueSignal&) {}
            catch (BreakSignal&)    { break; }
#ifdef VENOS_WASM
            if (++guard > 10'000'000)
                throw LangError("반복 횟수가 너무 많습니다 (무한 루프?)");
#endif
        }
    }
};
// for i = 1 to 10 (step 2) { ... }  — 양끝 포함, step 생략 시 방향 자동
struct ForStmt : Stmt {
    string var; ExprP start, end, step; StmtP body; int line;
    void exec(Env& env) override {
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
            *slot = Value::number(i);
            try { body->exec(env); }
            catch (ContinueSignal&) {}
            catch (BreakSignal&)    { return; }
        }
    }
};
// for x in xs { ... } — 리스트/문자열 순회 (스냅샷 방식: 순회 중 수정해도 안전)
struct ForEachStmt : Stmt {
    string var; ExprP iter; StmtP body; int line;
    void exec(Env& env) override {
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
            *slot = e;
            try { body->exec(env); }
            catch (ContinueSignal&) {}
            catch (BreakSignal&)    { return; }
        }
    }
};

struct BreakStmt : Stmt {
    void exec(Env&) override { throw BreakSignal{}; }
};
struct ContinueStmt : Stmt {
    void exec(Env&) override { throw ContinueSignal{}; }
};
struct ReturnStmt : Stmt {
    ExprP val;
    void exec(Env& env) override {
        throw ReturnSignal{ val ? val->eval(env) : Value::number(0) };
    }
};
struct FuncStmt : Stmt {
    string name;
    std::vector<string> params;
    StmtP body;
    void exec(Env&) override { g_funcs[name] = this; }
};

// class 이름 { func ... }  — 메서드 묶음. init 이 생성자.
struct ClassStmt : Stmt {
    string name;
    std::vector<std::unique_ptr<FuncStmt>> methodList;   // 소유권
    std::map<string, FuncStmt*> methods;                  // 이름 → 메서드
    void exec(Env&) override { g_classes[name] = this; }
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
    DepthGuard(int line) {
        if (++g_callDepth > MAX_RECURSION) {
            --g_callDepth;
            throw LangError(lineTag(line) + "함수 호출이 너무 깊습니다 (재귀 " 
                            + std::to_string(MAX_RECURSION) + "회 초과 — 무한 재귀?)");
        }
    }
    ~DepthGuard() { --g_callDepth; }
};

Value MethodCallExpr::eval(Env& env) {
    Value obj = target->eval(env);
    auto err = [&](const string& m) {
        return LangError(lineTag(line) + "" + m);
    };
    if (obj.kind != Value::OBJ)
        throw err(obj.kindName() + "에는 메서드를 호출할 수 없습니다");
    auto cit = g_classes.find(obj.className);
    if (cit == g_classes.end()) throw err("알 수 없는 클래스: " + obj.className);
    auto mit = cit->second->methods.find(method);
    if (mit == cit->second->methods.end())
        throw err("클래스 '" + obj.className + "' 에 메서드 '" + method + "' 이(가) 없습니다");
    FuncStmt* fn = mit->second;
    if (args.size() != fn->params.size())
        throw err(method + "() 는 인자 " + std::to_string(fn->params.size())
                  + "개가 필요합니다 (지금 " + std::to_string(args.size()) + "개)");
    std::vector<Value> vals;
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
        if (name == "round") { needArgs(1, "round(숫자)"); return Value::number(std::round(needNum(0))); }
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
                string s = trim(vals[0].str);
                try {
                    size_t used = 0;
                    double d = std::stod(s, &used);
                    if (used == s.size()) return Value::number(d);
                } catch (...) {}
                throw err("숫자로 바꿀 수 없는 문자열: \"" + vals[0].str + "\"");
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
            needArgs(2, "find(문자열, 찾을것)");
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
            std::ifstream f(toPath(needStr(0)));
            if (!f) throw err("파일을 열 수 없습니다: " + vals[0].str);
            std::stringstream buf;
            buf << f.rdbuf();
            return Value::text(buf.str());
        }
        if (name == "writefile") { // writefile("out.txt", 내용) → 파일에 저장
            needArgs(2, "writefile(경로, 내용)");
            std::ofstream f(toPath(needStr(0)));
            if (!f) throw err("파일을 만들 수 없습니다: " + vals[0].str);
            f << vals[1].toString();
            return Value::number(1);
        }
        if (name == "time") {      // time() → 1970년부터 지난 초 (소수점 포함)
            needArgs(0, "time()");
            auto now = std::chrono::system_clock::now().time_since_epoch();
            return Value::number(std::chrono::duration<double>(now).count());
        }
        if (name == "exists") {    // exists("save.txt") → 파일 있으면 true
            needArgs(1, "exists(경로)");
            std::ifstream f(toPath(needStr(0)));
            return Value::number(f.good() ? 1 : 0);
        }
        if (name == "appendfile") { // appendfile(경로, 내용) → 파일 끝에 이어쓰기
            needArgs(2, "appendfile(경로, 내용)");
            std::ofstream f(toPath(needStr(0)), std::ios::app);
            if (!f) throw err("파일을 열 수 없습니다: " + vals[0].str);
            f << vals[1].toString();
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
        if (name == "has") {       // has(d, "키") → true/false
            needArgs(2, "has(딕셔너리, 키)");
            if (vals[0].kind != Value::MAP) throw err("has() 의 1번째 인자는 딕셔너리여야 합니다");
            return Value::number(vals[0].map->count(needStr(1)) ? 1 : 0);
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
        if (it == g_funcs.end()) throw err("정의되지 않은 함수 또는 클래스: " + name);
        FuncStmt* fn = it->second;
        if (vals.size() != fn->params.size())
            throw err(name + "() 는 인자 " + std::to_string(fn->params.size())
                      + "개가 필요합니다 (지금 " + std::to_string(vals.size()) + "개)");
        DepthGuard guard(line);   // 무한 재귀 방지
        Env local;
        local.parent = g_global;
        for (size_t i = 0; i < vals.size(); i++)
            local.define(fn->params[i], vals[i]);
        try {
            fn->body->exec(local);
        } catch (ReturnSignal& r) {
            return r.v;
        }
        return Value::number(0);
    }
};

Value runMethod(ClassStmt* cls, FuncStmt* fn, Value& self,
                std::vector<Value>& args, int line) {
    (void)cls;
    DepthGuard guard(line);
    Env local;
    local.parent = g_global;
    local.define("self", self);   // self 는 같은 필드 맵을 공유 → 수정이 원본에 반영
    for (size_t i = 0; i < args.size(); i++)
        local.define(fn->params[i], args[i]);
    try {
        fn->body->exec(local);
    } catch (ReturnSignal& r) {
        return r.v;
    }
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
struct Parser {
    std::vector<Token> toks;
    size_t pos = 0;
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
            throw LangError(lineTag(peek().line) + "문법 오류: " + what + " 이(가) 필요합니다");
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
            node->name = expect(Tok::IDENT, "클래스 이름").text;
            expect(Tok::LBRACE, "{");
            while (!check(Tok::RBRACE) && !check(Tok::END)) {
                if (!check(Tok::FUNC))
                    throw LangError(lineTag(peek().line) + "클래스 안에는 " + KW_FUNC + " (메서드)만 쓸 수 있습니다");
                StmtP m = parseStatement();
                auto* fp = static_cast<FuncStmt*>(m.release());
                node->methods[fp->name] = fp;
                node->methodList.emplace_back(fp);
            }
            expect(Tok::RBRACE, "}");
            return node;
        }
        if (match(Tok::FUNC)) {
            auto node = std::make_unique<FuncStmt>();
            node->name = expect(Tok::IDENT, "함수 이름").text;
            expect(Tok::LPAREN, "(");
            if (!check(Tok::RPAREN)) {
                do {
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
            throw LangError(lineTag(line) + "문법 오류: = 이(가) 필요합니다");
        }
        throw LangError(lineTag(line) + "문법 오류: 문장이 될 수 없는 토큰입니다");
    }

    StmtP parseBlock() {
        expect(Tok::LBRACE, "{");
        auto block = std::make_unique<BlockStmt>();
        while (!check(Tok::RBRACE) && !check(Tok::END))
            block->stmts.push_back(parseStatement());
        expect(Tok::RBRACE, "}");
        return block;
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
        if (match(Tok::NOT))
            return std::make_unique<NotExpr>(parseNot());
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
    ExprP parseUnary() {
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
                        do { mc->args.push_back(parseExpr()); } while (match(Tok::COMMA));
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
                        + "문자열 보간의 { 에 짝이 되는 } 가 없습니다 (진짜 { 를 쓰려면 {{)");
                string inner = s.substr(i + 1, j - i - 1);
                if (trim(inner).empty())
                    throw LangError(lineTag(line) + "문자열 보간 {} 안이 비어 있습니다");
                if (!lit.empty()) { parts.push_back(std::make_unique<StrExpr>(lit)); lit.clear(); }
                try {
                    Parser sub(lex(inner));
                    ExprP e = sub.parseExpr();
                    if (!sub.check(Tok::END)) throw LangError("남는 토큰");
                    parts.push_back(std::move(e));
                } catch (LangError&) {
                    throw LangError(lineTag(line)
                        + "문자열 보간 {" + inner + "} 안의 식이 잘못되었습니다");
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
                    do { args.push_back(parseExpr()); } while (match(Tok::COMMA));
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
                do { list->items.push_back(parseExpr()); } while (match(Tok::COMMA));
            }
            expect(Tok::RBRACKET, "]");
            return list;
        }
        if (match(Tok::LBRACE)) {   // 딕셔너리 리터럴 {"키": 값, ...}
            auto m = std::make_unique<MapExpr>();
            m->line = t.line;
            if (!check(Tok::RBRACE)) {
                do {
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
        for (auto& stmt : program) stmt->exec(global);
    } catch (ExitSignal&) {
        g_global = nullptr;
        return;                       // exit() = 정상 종료
    } catch (BreakSignal&) {
        g_global = nullptr;
        throw LangError(KW_BREAK + " 는 반복문 안에서만 쓸 수 있습니다");
    } catch (ContinueSignal&) {
        g_global = nullptr;
        throw LangError(KW_CONTINUE + " 는 반복문 안에서만 쓸 수 있습니다");
    } catch (ReturnSignal&) {
        g_global = nullptr;
        throw LangError(KW_RETURN + " 은 함수 안에서만 쓸 수 있습니다");
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
struct RunErr : std::runtime_error { RunErr(const string& m) : std::runtime_error(m) {} };
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
        throw RunErr(a.kindName() + "와(과) " + b.kindName() + "는 이 연산이 안 됩니다");
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
static Value vmul(const Value& a, const Value& b) { needNums(a, b); return Value(a.num * b.num); }
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
    if (a.kind != b.kind) throw RunErr("숫자와 문자열은 비교할 수 없습니다");
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
    if (t.kind != Value::LIST) throw RunErr(t.kindName() + "에는 [ ] 를 쓸 수 없습니다");
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
    if (t.kind != Value::LIST) throw RunErr(t.kindName() + "에는 [ ] 를 쓸 수 없습니다");
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
    if (t.kind != Value::LIST) throw RunErr(t.kindName() + "에는 [ ] 를 쓸 수 없습니다");
    return (*t.list)[chkIdx(i, t.list->size())];
}
static void my_print(std::initializer_list<string> vs) {
    string o; bool first = true;
    for (auto& v : vs) { if (!first) o += " "; first = false; o += v; }
    std::cout << o << "\n";
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
static Value my_input(const string& prompt) {
    if (!prompt.empty()) std::cout << prompt << std::flush;
    string line;
    if (!rt_readline(line)) throw RunErr("입력을 읽을 수 없습니다");
    line = trimS(line);
    try { size_t u = 0; double d = std::stod(line, &u); if (u == line.size()) return Value(d); } catch (...) {}
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
        string s = trimS(v.str);
        try { size_t u = 0; double d = std::stod(s, &u); if (u == s.size()) return Value(d); } catch (...) {}
        throw RunErr("숫자로 바꿀 수 없는 문자열: \"" + v.str + "\"");
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
static Value b_readfile(const Value& a) {
    std::ifstream f(needStrR(a, "readfile"));
    if (!f) throw RunErr("파일을 열 수 없습니다: " + a.str);
    std::ostringstream buf; buf << f.rdbuf();
    return Value(buf.str());
}
static Value b_writefile(const Value& a, const Value& b) {
    std::ofstream f(needStrR(a, "writefile"));
    if (!f) throw RunErr("파일을 만들 수 없습니다: " + a.str);
    f << b.toString();
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
    std::ifstream f(needStrR(a, "exists"));
    return Value(f.good() ? 1.0 : 0.0);
}
static Value b_appendfile(const Value& a, const Value& b) {
    std::ofstream f(needStrR(a, "appendfile"), std::ios::app);
    if (!f) throw RunErr("파일을 열 수 없습니다: " + a.str);
    f << b.toString();
    return Value(1.0);
}
static Value b_keys(const Value& v) {
    if (v.kind != Value::MAP) throw RunErr("keys() 의 인자는 딕셔너리여야 합니다");
    Value out; out.kind = Value::LIST; out.list = std::make_shared<List>();
    for (auto& kv : *v.map) out.list->push_back(Value(kv.first));
    return out;
}
static Value b_has(const Value& v, const Value& k) {
    if (v.kind != Value::MAP) throw RunErr("has() 의 1번째 인자는 딕셔너리여야 합니다");
    return Value(v.map->count(mapKey(k)) ? 1.0 : 0.0);
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
        {"remove", {2, "b_remove"}},
    };

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
            if (!declared(v->name)) throw err(v->line, "정의되지 않은 변수: " + v->name);
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
                if ((int)c->args.size() != bi->second.first)
                    throw err(c->line, c->name + "() 는 인자 " + std::to_string(bi->second.first)
                              + "개가 필요합니다 (지금 " + std::to_string(c->args.size()) + "개)");
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
            if (uf == funcs.end()) throw err(c->line, "정의되지 않은 함수 또는 클래스: " + c->name);
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
            if (!declared(a->name))
                throw err(a->line, "선언되지 않은 변수에 대입: " + a->name
                          + "  (" + KW_LET + " " + a->name + " = ... 로 먼저 선언하세요)");
            ind(out, depth);
            out << varName(a->name) << " = " << genExpr(a->val.get()) << ";\n";
            return;
        }
        if (auto* pa = dynamic_cast<PathAssignStmt*>(s)) {
            if (!declared(pa->name)) throw err(pa->line, "정의되지 않은 변수: " + pa->name);
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
            if (!declared(pc->name)) throw err(pc->line, "정의되지 않은 변수: " + pc->name);
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
                     << "        throw RunErr(__self.kindName() + \"에는 메서드를 호출할 수 없습니다\");\n";
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
                     << mname << "' 이(가) 없습니다\");\n}\n\n";
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
    bool inFunc = false;
    bool sawMap = false;                     // 딕셔너리가 존재할 수 있는가 (1차 통과에서 알아낸다)
    bool sawList = false;                    // 리스트가 존재할 수 있는가
    bool sawIndex = false;                   // [ ] 인덱싱을 쓰는가 (머리말에 1부터 얘기를 넣을지)
    bool sawFloat = false;                   // 소수가 나올 수 있는가 (/ · sqrt · 입력 등)
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
        if (std::fabs(v) < 9.0e18 && v == (long long)v) snprintf(b, sizeof b, "%lld", (long long)v);
        else                                            snprintf(b, sizeof b, "%.17g", v);
        return b;
    }
    string need(const string& h) { helpers.insert(h); return "_" + h; }

    // ---- 우선순위 (Venos 와 파이썬이 같은 순서라 괄호를 최소로 낼 수 있다) ----
    enum { P_OR = 0, P_AND, P_NOT, P_CMP, P_ADD, P_MUL, P_UNARY, P_ATOM };
    static int precOf(Expr* e) {
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

    // 문자열이 확실한 식인가 — Venos 의 "문자열 + 숫자" 자동 변환을 어디서 흉내낼지 판단
    static bool stringish(Expr* e) {
        if (dynamic_cast<StrExpr*>(e)) return true;
        if (auto* b = dynamic_cast<BinExpr*>(e))
            return b->interpN > 0
                || (b->op == Tok::PLUS && (stringish(b->lhs.get()) || stringish(b->rhs.get())));
        if (auto* c = dynamic_cast<CallExpr*>(e)) {
            static const std::set<string> S = {"str","upper","lower","join","replace","substr","readfile"};
            return S.count(c->name) > 0;
        }
        return false;
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
        double k;
        if (constInt(e, k)) return true;
        if (auto* v = dynamic_cast<VarExpr*>(e)) return intVars.count(v->name) > 0;
        if (auto* c = dynamic_cast<CallExpr*>(e)) return c->name == "len" && !funcs.count("len");
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
        string o = "f\"";
        for (Expr* p : parts) {
            if (auto* s = dynamic_cast<StrExpr*>(p)) {
                for (char c : s->s) {
                    if (c == '{')       o += "{{";
                    else if (c == '}')  o += "}}";
                    else if (c == '"')  o += "\\\"";
                    else if (c == '\\') o += "\\\\";
                    else if (c == '\n') o += "\\n";
                    else if (c == '\t') o += "\\t";
                    else                o += c;
                }
            } else {
                // print 와 같은 규칙 — 파이썬 표기가 새어 나올 수 있으면 _show() 로 감싼다.
                // (감싸지 않으면 True / 5.0 / {'a': 1} / 83.33333333333333 이 찍힌다)
                o += plainSafe(p) ? "{" + expr(p) + "}"
                                  : "{" + need("show") + "(" + expr(p) + ")}";
            }
        }
        return o + "\"";
    }

    // ---- 표현식 ----
    string expr(Expr* e) {
        if (auto* n = dynamic_cast<NumExpr*>(e)) {
            if (n->v != (long long)n->v) sawFloat = true;
            return pyNum(n->v);
        }
        if (auto* s = dynamic_cast<StrExpr*>(e))  return pyStr(s->s);
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
            return indexGet(ix->target.get(), ix->index.get());
        if (auto* f = dynamic_cast<FieldExpr*>(e))
            return wrap(f->target.get(), P_ATOM) + "." + pyName(f->field);
        if (auto* mc = dynamic_cast<MethodCallExpr*>(e)) {
            string o = wrap(mc->target.get(), P_ATOM) + "." + pyName(mc->method) + "(";
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
        // Venos 의 not/and/or 는 1/0 을 낸다 — 파이썬 bool 이 그대로 찍히지 않도록 int() 로 맞춘다
        if (auto* n = dynamic_cast<NotExpr*>(e))  return "int(not " + wrap(n->inner.get(), P_NOT + 1) + ")";
        if (auto* lg = dynamic_cast<LogicalExpr*>(e)) {
            int p = (lg->op == Tok::AND) ? P_AND : P_OR;   // 내부 자식용 (밖에서는 원자)
            string op = (lg->op == Tok::AND) ? " and " : " or ";
            return "int(bool(" + wrap(lg->lhs.get(), p) + ")" + op
                 + "bool(" + wrap(lg->rhs.get(), p + 1) + "))";
        }
        if (auto* in = dynamic_cast<InputExpr*>(e)) {
            sawFloat = true;   // 사용자가 1.5 를 칠 수도 있다
            return need("input") + "(" + (in->prompt.empty() ? "" : pyStr(in->prompt)) + ")";
        }
        if (auto* c = dynamic_cast<CallExpr*>(e)) return call(c);
        throw LangError("파이썬으로 변환할 수 없는 식이 있습니다");
    }

    // xs[1] 은 첫 번째, d["키"] 는 키 조회 — 리터럴이면 그 자리에서 정하고,
    // 변수라서 알 수 없으면 도우미로 넘긴다.
    string indexGet(Expr* target, Expr* index) {
        sawIndex = true;
        string T = wrap(target, P_ATOM);
        if (dynamic_cast<StrExpr*>(index)) return T + "[" + expr(index) + "]";
        if (auto* n = dynamic_cast<NumExpr*>(index)) return T + "[" + pyNum(n->v - 1) + "]";
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
        if (f == "len")   { need2(1); return "len(" + A(0) + ")"; }
        if (f == "abs")   { need2(1); return "abs(" + A(0) + ")"; }
        if (f == "min")   { need2(2); return "min(" + A(0) + ", " + A(1) + ")"; }
        if (f == "max")   { need2(2); return "max(" + A(0) + ", " + A(1) + ")"; }
        if (f == "floor") { need2(1); imports.insert("math"); return "math.floor(" + A(0) + ")"; }
        if (f == "ceil")  { need2(1); imports.insert("math"); return "math.ceil("  + A(0) + ")"; }
        if (f == "sqrt")  { need2(1); imports.insert("math"); sawFloat = true; return "math.sqrt("  + A(0) + ")"; }
        if (f == "round") { need2(1); imports.insert("math"); return need("round") + "(" + A(0) + ")"; }
        if (f == "random"){ need2(2); imports.insert("random"); return need("random") + "(" + A(0) + ", " + A(1) + ")"; }
        if (f == "time")  { need2(0); imports.insert("time"); sawFloat = true; return "time.time()"; }
        if (f == "num")   { need2(1); sawFloat = true; return need("num")  + "(" + A(0) + ")"; }
        if (f == "str")   { need2(1); return need("show") + "(" + A(0) + ")"; }
        if (f == "push")  { need2(2); sawList = true; return need("push") + "(" + A(0) + ", " + A(1) + ")"; }
        if (f == "pop")   { need2(1); return atom(0) + ".pop()"; }
        if (f == "sort")  { need2(1); sawList = true; return need("sort") + "(" + A(0) + ")"; }
        if (f == "keys")  { need2(1); sawMap = true; sawList = true; return "sorted(" + atom(0) + ".keys())"; }
        if (f == "has")   { need2(2); sawMap = true;
                            return "int(" + wrap(c->args[1].get(), P_CMP + 1) + " in "
                                          + wrap(c->args[0].get(), P_CMP + 1) + ")"; }
        if (f == "remove"){ need2(2); return need("remove") + "(" + A(0) + ", " + A(1) + ")"; }
        if (f == "split") { need2(2); sawList = true; return atom(0) + ".split(" + A(1) + ")"; }
        if (f == "join")  { need2(2); return need("join") + "(" + A(0) + ", " + A(1) + ")"; }
        if (f == "upper") { need2(1); return atom(0) + ".upper()"; }
        if (f == "lower") { need2(1); return atom(0) + ".lower()"; }
        // find 는 + 1 이 붙으므로 통째로 괄호를 씌운다 (find(s,x) * 10 이 s.find(x) + 1 * 10 이 되면 안 된다)
        if (f == "find")  { need2(2); return "(" + atom(0) + ".find(" + A(1) + ") + 1)"; }
        if (f == "replace"){need2(3); return atom(0) + ".replace(" + A(1) + ", " + A(2) + ")"; }
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

    // 대입 대상이 전역이면 함수 안에서 global 선언이 필요하다
    void noteAssign(const string& name) {
        if (inFunc && !localSet.count(name) && globalSet.count(name)) touchedGlobals.insert(name);
        // for 루프 변수에 다시 대입하면 더 이상 정수라고 볼 수 없다
        intVars.erase(name);
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
            noteAssign(a->name);
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
            noteAssign(pa->name);
            o << pad(d) << lvalue(pa->name, pa->path) << " = " << expr(pa->val.get()) << "\n";
            return;
        }
        if (auto* pc = dynamic_cast<PathCompoundStmt*>(s)) {
            noteAssign(pc->name);
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
            o << pad(d) << expr(es->e.get()) << "\n";
            return;
        }
        if (auto* b = dynamic_cast<BlockStmt*>(s)) {
            for (auto& c : b->stmts) stmt(c.get(), o, d);
            return;
        }
        if (auto* i = dynamic_cast<IfStmt*>(s)) {
            o << pad(d) << "if " << expr(i->cond.get()) << ":\n";
            body(i->thenB.get(), o, d + 1);
            Stmt* els = i->elseB.get();
            while (els) {
                if (auto* chain = dynamic_cast<IfStmt*>(els)) {   // else if → elif
                    o << pad(d) << "elif " << expr(chain->cond.get()) << ":\n";
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
            o << pad(d) << "while " << expr(w->cond.get()) << ":\n";
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
            // 리터럴이면 그대로 돈다. 변수면 딕셔너리일 수 있어 도우미를 거친다
            // (Venos 는 딕셔너리를 키 정렬 순서로 순회한다).
            string src = (!sawMap || dynamic_cast<ListExpr*>(it) || dynamic_cast<StrExpr*>(it))
                       ? expr(it) : need("iter") + "(" + expr(it) + ")";
            o << pad(d) << "for " << pyName(fe->var) << " in " << src << ":\n";
            body(fe->body.get(), o, d + 1);
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
            lastRangeIsInt = false;             // _rng 는 소수도 내줄 수 있다
            return need("rng") + "(" + a + ", " + b + ")";
        }
        if (kb && constInt(f->step.get(), st) && st != 0)
            return "range(" + a + ", " + pyNum(sb + (st > 0 ? 1 : -1)) + ", " + pyNum(st) + ")";
        lastRangeIsInt = false;
        return need("rng") + "(" + a + ", " + b + ", " + expr(f->step.get()) + ")";
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

        std::ostringstream out;
        out << "# 이 파일은 Venos 프로그램을 파이썬으로 옮긴 것입니다 (venos topython).\n";
        int noteN = 0;
        if (sawIndex || sawFloat || !helpers.empty())
            out << "#\n# Venos 와 파이썬이 다른 점 — 숨기지 않고 적어 둡니다:\n";
        if (sawIndex)
            out << "#   " << ++noteN << ") 리스트를 Venos 는 1번부터, 파이썬은 0번부터 셉니다."
                   " 그래서 xs[1] 이 xs[0] 이 됩니다.\n";
        if (sawFloat)
            out << "#   " << ++noteN << ") 소수를 보여주는 방식이 다릅니다. Venos 는 5.0 을 5 로,"
                   " 91.66666...을 91.6667 로\n"
                   "#      줄여서 보여주지만 파이썬은 있는 그대로 보여줍니다.\n";
        if (!helpers.empty())
            out << "#   " << ++noteN << ") 밑줄로 시작하는 _이름 함수들은 Venos 와 똑같이 보이게 하려고"
                   " 붙인 것뿐이니\n"
                   "#      파이썬을 배울 때는 신경 쓰지 않아도 됩니다.\n";
        if (!imports.empty()) {
            out << "\n";
            for (auto& m : imports) out << "import " << m << "\n";
        }
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
            {"num",
             "def _num(s):\n"
             "    if isinstance(s, (int, float)): return s\n"
             "    f = float(str(s).strip())\n"
             "    return int(f) if f == int(f) else f\n"},
            {"input",
             "def _input(prompt=\"\"):\n"
             "    s = input(prompt).strip()\n"
             "    try:\n"
             "        f = float(s)\n"
             "        return int(f) if f == int(f) else f\n"
             "    except ValueError:\n"
             "        return s\n"},
            {"idx",
             "def _idx(c, k):\n"
             "    return c[k] if isinstance(c, dict) else c[int(k) - 1]\n"},
            {"k",
             "def _k(c, i):\n"
             "    return i if isinstance(c, dict) else int(i) - 1\n"},
            {"push",  "def _push(xs, v):\n    xs.append(v)\n    return xs\n"},
            {"sort",  "def _sort(xs):\n    xs.sort()\n    return xs\n"},
            {"join",  "def _join(xs, sep):\n    return sep.join(_show(x) for x in xs)\n"},
            {"substr","def _substr(s, start, n):\n    i = int(start) - 1\n    return s[i:i + int(n)]\n"},
            {"remove",
             "def _remove(c, k):\n"
             "    if isinstance(c, dict):\n"
             "        return int(c.pop(k, None) is not None)\n"
             "    return c.pop(int(k) - 1)\n"},
            {"round", "def _round(x):\n    return math.floor(x + 0.5) if x >= 0 else math.ceil(x - 0.5)\n"},
            {"random","def _random(a, b):\n    a, b = int(a), int(b)\n    if a > b: a, b = b, a\n    return random.randint(a, b)\n"},
            {"rng",
             "def _rng(a, b, s=1):\n"
             "    if a == int(a) and b == int(b) and s == int(s):\n"
             "        a, b, s = int(a), int(b), int(s)\n"
             "        return range(a, b + (1 if s > 0 else -1), s)\n"
             "    out, x = [], a\n"
             "    while (x <= b) if s > 0 else (x >= b):\n"
             "        out.append(x)\n"
             "        x += s\n"
             "    return out\n"},
            {"iter",  "def _iter(v):\n    return sorted(v) if isinstance(v, dict) else v\n"},
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

void cmdBuild(const string& arg) {
    if (currentFile.empty()) { std::cout << "choose 로 파일을 먼저 선택하세요\n"; return; }
    string fname = currentFile;

    string base = fname.substr(0, fname.size() - FILE_EXT.size());
    string cppName = base + ".cpp";
#ifdef _WIN32
    string exeName = base + ".exe";
    string runCmd  = base + ".exe";
#else
    string exeName = base;
    string runCmd  = "./" + base;
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
        return;
    }
    {
        std::ofstream out(toPath(cppName));
        out << cppCode;
    }
    std::cout << "C++ generated: " << cppName << "\n";
    bool nonAscii = false;
    for (unsigned char ch : fname) if (ch >= 0x80) nonAscii = true;
    if (nonAscii)
        std::cout << "(note: non-ASCII filenames can break the g++ call on Windows — ASCII names recommended)\n";
    std::cout << "compiling with g++...\n";
    string compile = "g++ -std=c++17 -O2 -o \"" + exeName + "\" \"" + cppName + "\"";
    std::cout << std::flush;
    int rc = std::system(compile.c_str());
    if (rc != 0) {
        std::cout << "!! g++ failed (is g++ installed?)\n";
        std::cout << "   the generated C++ is still there, you can compile it yourself: " << cppName << "\n";
        return;
    }
    std::cout << "build OK: " << exeName << "  (run: " << runCmd << ")\n";
    if (arg == "run") {
        std::cout << "----- run -----\n" << std::flush;
        int rrc = std::system(runCmd.c_str());
        if (rrc != 0) std::cout << "(program exited with code " << rrc << ")\n";
    }
}

// topython 명령: .my → 읽을 수 있는 .py
// build 와 달리 컴파일하지 않는다 — 학생이 읽고 다음 언어로 넘어가라고 내주는 파일이다.
void cmdTopython() {
    if (currentFile.empty()) { std::cout << "choose 로 파일을 먼저 선택하세요\n"; return; }
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
        return;
    }
    {
        std::ofstream out(toPath(pyName));
        out << pyCode;
    }
    std::cout << "Python generated: " << pyName << "  (run: python3 " << pyName << ")\n";
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
    try {
        runSource(src);
        std::cout << "=== done ===\n";
    } catch (const LangError& e) {
        printError(e.what());
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
    std::cout << "=== Venos REPL ===  (:q 나가기)\n";
    std::cout << "한 줄씩 바로 실행됩니다. 값만 입력하면 결과를 출력해요 (예: 3 * 7)\n\n";
    Env env;
    g_funcs.clear();
    g_classes.clear();
    g_callDepth = 0;
    g_lineMap.clear();
    g_srcLines.clear();
    g_global = &env;
    std::vector<StmtP> keepAlive;   // 함수/클래스 AST 소유권 유지용
    string line;
    while (true) {
        std::cout << ">> " << std::flush;
        if (!readLine(line)) break;
        string t = trim(line);
        if (t == ":q" || t == "exit") break;
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
                for (auto& s : prog) s->exec(env);
            });
        } catch (ExitSignal&) {
            break;
        } catch (LangError& e) {
            printError(e.what());
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
                printError(e.what());
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

void cmdRun() {
    if (currentFile.empty()) { std::cout << "choose 로 파일을 먼저 선택하세요\n"; return; }
    std::cout << "=== running: " << currentFile << " ===\n";
    try {
        runSourceBigStack(expandImports(currentFile));
        std::cout << "=== done ===\n";
    } catch (const LangError& e) {
        printError(e.what());
    } catch (const std::exception& e) {
        std::cout << "!! 내부 에러: " << e.what() << "\n";
    }
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

int main(int argc, char** argv) {
#ifdef _WIN32
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
        if (a1 == "topython" && argc >= 3) {
            string f = withExt(argv[2]);
            if (!fs::exists(toPath(f))) { std::cout << "파일 없음: " << f << "\n"; return 1; }
            currentFile = f;
            cmdTopython();
            return 0;
        }
        if (a1 == "build" && argc >= 3) {
            string f = withExt(argv[2]);
            if (!fs::exists(toPath(f))) { std::cout << "파일 없음: " << f << "\n"; return 1; }
            currentFile = f;
            cmdBuild((argc >= 4 && string(argv[3]) == "run") ? "run" : "");
            return 0;
        }
        string f = withExt((a1 == "run" && argc >= 3) ? argv[2] : a1);
        if (!fs::exists(toPath(f))) { std::cout << "파일 없음: " << f << "\n"; return 1; }
        currentFile = f;
        cmdRun();
        return 0;
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
        else std::cout << "알 수 없는 명령어: " << cmd << "  (help 참고)\n";
    }
    std::cout << "종료합니다.\n";
    return 0;
}
#endif  // VENOS_WASM 아님 (네이티브 셸 끝)
