#!/usr/bin/env node
// 내장 함수가 네 곳 전부에 있는지 본다.
//
//   node tools/check-builtins.js
//
// 철칙은 "언어 기능을 더하면 인터프리터·트랜스파일러·PyGen 에 동시에 구현한다"인데,
// 내장 함수는 특히 잊기 쉽다 — 한 줄만 더하면 되는 일이라 나머지 세 곳을 안 들르게 된다.
// 잊었을 때 나타나는 모습이 각각 다르고, 전부 조용하다:
//
//   인터프리터에만  → build 가 "정의되지 않은 함수" 로 거절 (그나마 시끄러운 쪽)
//   BUILTIN_NAMES 빠짐 → 오타 제안이 그 이름을 후보로 못 삼는다
//   PyGen 빠짐      → topython 이 거절. 약속한 다리가 끊긴다
//   vscode-venos 빠짐 → 편집기에서 그 이름만 색이 안 입는다 (키워드도 같다)
//   VENOS_SPEC 빠짐 → 명세만 읽는 사람(과 AI)에게는 그 기능이 없는 것과 같다
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

// 5) VSCode 확장의 문법 강조 — 여기만 빠지면 새 내장 함수가 회색으로 나온다
const tm = JSON.parse(fs.readFileSync(
    path.join(__dirname, '..', 'vscode-venos', 'syntaxes', 'venos.tmLanguage.json'), 'utf8'));
const matches = [];
(function walk(o) {
    if (Array.isArray(o)) o.forEach(walk);
    else if (o && typeof o === 'object')
        for (const [k, v] of Object.entries(o)) {
            if (k === 'match' && typeof v === 'string') matches.push(v);
            else walk(v);
        }
})(tm);
const alternation = (re, what) => {
    const line = matches.find((m) => re.test(m));
    if (!line) { console.error(`tmLanguage 에서 ${what} 패턴을 못 찾았습니다.`); process.exit(2); }
    // "\b(a|b|c)(?=...)" 에서 괄호 안의 이름만 꺼낸다 (\b 의 b 가 섞이지 않게)
    const group = line.match(/\(([a-z_]+(?:\|[a-z_]+)+)\)/);
    if (!group) { console.error(`tmLanguage 의 ${what} 패턴에서 이름 목록을 못 읽었습니다.`); process.exit(2); }
    return group[1].split('|');
};
// 뒤에 "(" 가 오는지 보는 전방탐색이 붙은 패턴이 내장 함수 목록이다
const vscode = alternation(/\(\?=/, '내장 함수');

// ---- 키워드도 같은 이유로 대조한다 ----
// venos.cpp 에 키워드를 더하고 확장을 안 고치면 새 문법만 색이 안 입는다.
const kwSrc = [...src.matchAll(/static const string KW_[A-Z_]+\s*=\s*"([a-z]+)"/g)]
    .map((m) => m[1]);
// KW_ 상수가 아닌 키워드들 (렉서/로더가 문자열로 직접 본다)
const kwExtra = ['import', 'self'];
const kwWanted = [...new Set([...kwSrc, ...kwExtra])];

// tmLanguage 쪽: 내장 함수 패턴을 뺀 모든 교대 목록 + 따로 잡아 둔 self
const kwHave = new Set();
for (const m of matches) {
    if (/\(\?=/.test(m)) continue;                       // 내장 함수 패턴
    const g = m.match(/\(([a-z_]+(?:\|[a-z_]+)+)\)/);
    if (g) g[1].split('|').forEach((n) => kwHave.add(n));
    const solo = m.match(/^\\b([a-z_]+)\\b$/);
    if (solo) kwHave.add(solo[1]);
}
const kwMissing = kwWanted.filter((k) => !kwHave.has(k)).sort();
const kwExtraFound = [...kwHave].filter((k) => !kwWanted.includes(k)).sort();

const lists = {
    'BUILTIN_NAMES (오타 제안)': declared,
    '인터프리터': interp,
    '트랜스파일러': codegen,
    'PyGen (topython)': pygen,
    'VSCode 문법 강조': vscode,
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

if (kwMissing.length) {
    console.log(`✗ VSCode 문법 강조에 키워드 없음: ${kwMissing.join(', ')}`);
    bad++;
}
if (kwExtraFound.length) {
    console.log(`✗ VSCode 문법 강조에만 있는 키워드: ${kwExtraFound.join(', ')}`);
    bad++;
}

// ---- 명세에 다 적혀 있는가 ----
// VENOS_SPEC 은 "AI 에게 그대로 건네 주면 Venos 를 쓸 수 있다" 를 노린 문서다.
// 내장 함수나 키워드가 거기 없으면, 그걸 쓰는 법을 알 방법이 없다.
const specMissing = {};
for (const doc of ['VENOS_SPEC.md', 'VENOS_SPEC.en.md']) {
    const t = fs.readFileSync(path.join(__dirname, '..', doc), 'utf8');
    const word = (w) => new RegExp('(?<![A-Za-z_])' + w + '(?![A-Za-z_])').test(t);
    const miss = [...all].filter((n) => !word(n)).concat(kwWanted.filter((k) => !word(k))).sort();
    if (miss.length) specMissing[doc] = miss;
}

for (const [doc, miss] of Object.entries(specMissing)) {
    console.log(`✗ ${doc} 에 설명이 없음: ${miss.join(', ')}`);
    bad++;
}

if (bad) {
    console.log('\n내장 함수는 구현 다섯 곳(venos.cpp 넷 + vscode-venos)에, 키워드는 venos.cpp 와\nvscode-venos 양쪽에, 그리고 둘 다 VENOS_SPEC 두 벌에 적혀 있어야 합니다.');
    process.exit(1);
}
console.log(`내장 함수 ${all.size}개 · 키워드 ${kwWanted.length}개 — 구현 다섯 곳과 명세 두 벌 모두 일치`);
