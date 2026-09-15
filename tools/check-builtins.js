#!/usr/bin/env node
// 내장 함수가 네 곳 전부에 있는지 본다.
//
//   node tools/check-builtins.js
//
// 철칙은 "언어 기능을 더하면 인터프리터·트랜스파일러·PyGen 에 동시에 구현한다"인데,
// 내장 함수는 특히 잊기 쉽다 — 한 줄만 더하면 되는 일이라 나머지 세 곳을 안 들르게 된다.
// 잊었을 때 나타나는 모습이 각각 다르고, 셋 다 조용하다:
//
//   인터프리터에만  → build 가 "정의되지 않은 함수" 로 거절 (그나마 시끄러운 쪽)
//   BUILTIN_NAMES 빠짐 → 오타 제안이 그 이름을 후보로 못 삼는다
//   PyGen 빠짐      → topython 이 거절. 약속한 다리가 끊긴다
//
// 그래서 소스에서 네 목록을 뽑아 서로 같은지만 본다. 스위트가 테스트 케이스로
// 잡으려면 내장 함수마다 케이스를 써야 하는데, 이쪽이 싸고 빠짐없다.
'use strict';
const fs = require('fs');
const path = require('path');

const src = fs.readFileSync(path.join(__dirname, '..', 'venos.cpp'), 'utf8');

const between = (startRe, endRe, what) => {
    const s = src.search(startRe);
    if (s < 0) { console.error(`venos.cpp 에서 ${what} 의 시작을 못 찾았습니다.`); process.exit(2); }
    const e = src.slice(s).search(endRe);
    if (e < 0) { console.error(`venos.cpp 에서 ${what} 의 끝을 못 찾았습니다.`); process.exit(2); }
    return src.slice(s, s + e);
};

// 1) 오타 제안이 후보로 삼는 목록
const declared = [...between(/static const std::vector<string> BUILTIN_NAMES = \{/, /\n\};/, 'BUILTIN_NAMES')
    .matchAll(/"([a-z_]+)"/g)].map((m) => m[1]);

// 2) 인터프리터의 분기 (CallExpr::eval 안) — "정의되지 않은 함수" 로 끝난다
const interp = [...between(/struct CallExpr : Expr \{/, /정의되지 않은 함수/, '인터프리터 내장함수 분기')
    .matchAll(/name == "([a-z_]+)"/g)].map((m) => m[1]);

// 3) 트랜스파일러의 표
const codegen = [...between(/std::map<string, std::pair<int, string>> builtins = \{/, /\n    \};/, 'CodeGen builtins')
    .matchAll(/\{"([a-z_]+)", \{/g)].map((m) => m[1]);

// 4) PyGen 의 분기 — 역시 "정의되지 않은 함수" 로 끝난다
const pygen = [...between(/string call\(CallExpr\* c\) \{/, /정의되지 않은 함수/, 'PyGen call()')
    .matchAll(/f == "([a-z_]+)"/g)].map((m) => m[1]);

const lists = {
    'BUILTIN_NAMES (오타 제안)': declared,
    '인터프리터': interp,
    '트랜스파일러': codegen,
    'PyGen (topython)': pygen,
};

const all = new Set(Object.values(lists).flat());
let bad = 0;
for (const [label, names] of Object.entries(lists)) {
    const have = new Set(names);
    const missing = [...all].filter((n) => !have.has(n)).sort();
    if (missing.length) {
        console.log(`✗ ${label}: ${missing.join(', ')} 없음`);
        bad++;
    }
}

if (bad) {
    console.log('\n내장 함수는 네 곳 전부에 있어야 합니다 (venos.cpp).');
    process.exit(1);
}
console.log(`${all.size}개 — 네 곳 모두 일치`);
