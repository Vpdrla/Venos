#!/usr/bin/env node
// 레슨 트랙 점검 — docs/lessons.js 가 튜토리얼의 단일 진실 공급원이라, 여기가 틀리면
// 플레이그라운드의 레슨과 TUTORIAL.md 가 같이 틀린다.
//
//   node tools/check-lessons.js
//
// 세 가지를 본다:
//   1) 레슨 코드(ko·en)가 에러 없이 끝나는가 — 입력을 다 써서 끝나는 건 봐준다
//   2) 설명이 백틱으로 가리키는 이름이 그 언어의 코드에 실제로 있는가
//      (desc.en 은 `factorial` 이라는데 영어 코드에는 그 이름이 없는 상태를 잡는다)
//   3) TUTORIAL.md / TUTORIAL.ko.md 가 지금 lessons.js 로 다시 만든 것과 같은가
//      (손으로 고치면 다음 생성 때 조용히 사라지므로)
'use strict';
const fs = require('fs');
const path = require('path');
const os = require('os');
const { execFileSync, spawnSync } = require('child_process');

const ROOT = path.join(__dirname, '..');
// 윈도우에서는 g++ 가 확장자 없는 -o 에 .exe 를 붙인다
const VENOS = ['venos', 'venos.exe']
    .map((n) => path.join(ROOT, n))
    .find((p) => fs.existsSync(p)) || path.join(ROOT, 'venos');

// 입력을 쓰는 레슨이 끝까지 갈 수 있게 답을 넉넉히 넣어 준다.
// 전부 숫자로 둔다 — 숫자 맞히기처럼 입력을 수로 다루는 레슨이 섞여 있어서다
// (input 은 숫자처럼 보이는 입력만 숫자로 준다).
const STDIN = '25\n12\n37\n6\n43\n19\n31\n2\n48\n9\n'.repeat(40);

function loadLessons() {
    const src = fs.readFileSync(path.join(ROOT, 'docs', 'lessons.js'), 'utf8');
    const m = src.match(/const LESSONS = (\[[\s\S]*?\n\];)/);
    if (!m) { console.error('docs/lessons.js 에서 LESSONS 배열을 찾지 못했습니다.'); process.exit(2); }
    return eval(m[1]);   // 우리 저장소 안의 데이터 파일이다
}

// Venos 키워드·내장함수는 코드에 없어도 설명에 나올 수 있다
const KEYWORDS = new Set([
    'let', 'print', 'input', 'if', 'else', 'then', 'while', 'do', 'for', 'to', 'step', 'in',
    'break', 'continue', 'func', 'return', 'class', 'self', 'try', 'catch', 'import',
    'true', 'false', 'and', 'or', 'not',
    'random', 'round', 'floor', 'ceil', 'abs', 'sqrt', 'min', 'max', 'num', 'str',
    'len', 'push', 'pop', 'sort', 'reverse', 'remove', 'keys', 'has',
    'split', 'join', 'upper', 'lower', 'find', 'replace', 'substr',
    'readfile', 'writefile', 'appendfile', 'exists', 'time', 'exit', 'copy', 'error',
]);

let fails = 0;
const fail = (msg) => { console.log('  ✗ ' + msg); fails++; };

if (!fs.existsSync(VENOS)) {
    console.error('venos 실행 파일이 없습니다.  g++ -std=c++17 -O2 -o venos venos.cpp  먼저 실행하세요.');
    process.exit(2);
}

const lessons = loadLessons();
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'venos-lessons-'));

console.log(`레슨 ${lessons.length}개 점검\n`);

for (const lesson of lessons) {
    for (const lang of ['ko', 'en']) {
        const label = `${lesson.id} (${lang})`;
        const code = lesson.code && lesson.code[lang];
        if (typeof code !== 'string' || !code.trim()) { fail(`${label}: 코드가 비어 있습니다`); continue; }

        // ---- 1) 실행 ----
        const file = path.join(tmp, `${lesson.id}_${lang}.my`);
        fs.writeFileSync(file, code + '\n');
        const run = spawnSync(VENOS, [file], { input: STDIN, encoding: 'utf8', timeout: 20000 });
        if (run.error) { fail(`${label}: 실행 실패 — ${run.error.message}`); continue; }
        const out = (run.stdout || '') + (run.stderr || '');
        const errs = out.split('\n')
            .filter((l) => l.includes('!! 에러:') || l.includes('!! 내부 에러:'))
            // 준비한 입력을 다 쓴 것뿐이면 레슨 잘못이 아니다
            .filter((l) => !l.includes('입력을 읽을 수 없습니다'));
        if (errs.length) fail(`${label}: 실행 중 에러\n      ${errs[0].trim()}`);

        // ---- 2) 설명이 가리키는 이름이 코드에 있는가 ----
        const desc = (lesson.desc && lesson.desc[lang]) || '';
        for (const quoted of desc.match(/`[^`\n]+`/g) || []) {
            const name = quoted.slice(1, -1);
            // 산문 안의 식별자만 본다. 기호·연산자·문장 조각은 건너뛴다.
            if (!/^[A-Za-z_가-힣][A-Za-z0-9_가-힣]*$/.test(name)) continue;
            if (KEYWORDS.has(name)) continue;
            if (!code.includes(name)) fail(`${label}: 설명은 \`${name}\` 이라는데 코드에 그 이름이 없습니다`);
        }
    }
}

// ---- 3) TUTORIAL 이 최신인가 ----
const before = ['TUTORIAL.md', 'TUTORIAL.ko.md'].map((f) => fs.readFileSync(path.join(ROOT, f), 'utf8'));
try {
    execFileSync(process.execPath, [path.join(__dirname, 'gen-tutorial.js')], { cwd: ROOT, stdio: 'pipe' });
} catch (e) {
    fail('gen-tutorial.js 가 실패했습니다: ' + e.message);
}
const after = ['TUTORIAL.md', 'TUTORIAL.ko.md'].map((f) => fs.readFileSync(path.join(ROOT, f), 'utf8'));
['TUTORIAL.md', 'TUTORIAL.ko.md'].forEach((f, i) => {
    if (before[i] !== after[i])
        fail(`${f} 이 lessons.js 와 어긋나 있었습니다 (방금 다시 만들었으니 같이 커밋하세요)`);
});

fs.rmSync(tmp, { recursive: true, force: true });

if (fails) { console.log(`\n실패 ${fails}건`); process.exit(1); }
console.log('전부 통과');
