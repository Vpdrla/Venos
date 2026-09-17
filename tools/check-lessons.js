#!/usr/bin/env node
// 레슨 트랙 점검 — docs/lessons.js 가 튜토리얼의 단일 진실 공급원이라, 여기가 틀리면
// 플레이그라운드의 레슨과 TUTORIAL.md 가 같이 틀린다.
//
//   node tools/check-lessons.js
//
// 네 가지를 본다:
//   1) 레슨 코드(ko·en)가 에러 없이 끝나는가 — 입력을 다 써서 끝나는 건 봐준다
//   2) 같은 코드가 **세 방식에서 같은 답을 내는가** (인터프리터 / build / topython).
//      레슨은 학생이 실제로 돌리는 코드인데 오래 differential 밖에 있었다. 마지막 레슨이
//      "topython 으로 파이썬에 건너가기" 인 마당에, 그 파이썬이 같은 답을 내는지는
//      아무도 안 보고 있었다.
//   3) 설명이 백틱으로 가리키는 이름이 그 언어의 코드에 실제로 있는가
//      (desc.en 은 `factorial` 이라는데 영어 코드에는 그 이름이 없는 상태를 잡는다)
//   4) TUTORIAL.md / TUTORIAL.ko.md 가 지금 lessons.js 로 다시 만든 것과 같은가
//      (손으로 고치면 다음 생성 때 조용히 사라지므로)
'use strict';
const fs = require('fs');
const path = require('path');
const os = require('os');
const { execFileSync, spawnSync } = require('child_process');

// 파이썬 비교를 건너뛰는 레슨/예제와 그 이유 (러너의 PY_SKIP 과 같은 성격).
const PY_SKIP = {
    dict: 'catch 가 받는 문구가 Venos 것과 파이썬 것으로 다르다 ("키가 없습니다" vs \'gym\')',
    guess: 'random() 을 쓴다 — 파이썬의 난수는 우리 것과 수열이 다르다',
    errors: 'catch 가 받는 문구가 Venos 것과 파이썬 것으로 다르다 (topython 은 에러 문구까지 흉내내지 않는다)',
    project: 'random() 을 쓴다 — 파이썬의 난수는 우리 것과 수열이 다르다',
};
// 준비한 입력을 다 쓰면 양쪽이 **다른 모양으로** 보고한다 — Venos 는 한 줄,
// 파이썬은 역추적이다 (문구는 _input 이 맞춰 뒀지만 형식이 다르다). 거기서부터는
// 비교할 게 없으므로 잘라 낸다. 그 문구가 없는데 나온 역추적은 **자르지 않는다** —
// NameError 같은 진짜 코드젠 오류가 거기 숨으면 안 된다.
function cutAtInputEnd(text) {
    if (!text.includes('입력을 읽을 수 없습니다')) return text;
    const lines = text.split('\n');
    for (let i = 0; i < lines.length; i++)
        if (lines[i].includes('입력을 읽을 수 없습니다')
            || lines[i].includes('Traceback (most recent call last)'))
            return lines.slice(0, i).join('\n');
    return text;
}

// 배너·호출 경로·소스 줄 표시는 인터프리터에만 있다. catch 문구의 [줄 N] 도 마찬가지.
function normalize(text) {
    return cutAtInputEnd(String(text).replace(/\r/g, ''))
        .split('\n')
        .filter((l) => !l.startsWith('=== ') && !l.startsWith('    부른 순서: ')
                       && !/^ *줄 \d+ \| /.test(l))
        .map((l) => l.replace(/\[[^\]]*줄 \d+\] /g, ''))
        .map((l) => l.replace(/-?\d+\.\d+/g, (m) => String(parseFloat(m))))
        .join('\n');
}

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

// 어긋난 첫 줄만 보여 준다 — 레슨 출력은 길어서 전부 쏟으면 정작 다른 줄이 안 보인다
function firstDiff(want, got) {
    const a = want.split('\n'), b = got.split('\n');
    for (let i = 0; i < Math.max(a.length, b.length); i++)
        if (a[i] !== b[i])
            return `      ${i + 1}번째 줄\n        인터프리터: ${a[i] === undefined ? '(없음)' : a[i]}\n        저쪽:       ${b[i] === undefined ? '(없음)' : b[i]}`;
    return '      (줄 수만 다릅니다)';
}

// python3 이 없으면 파이썬 비교만 건너뛴다 (윈도우 CI 등)
const PY = ['python3', 'python'].find((c) => spawnSync(c, ['-c', 'pass'], { timeout: 10000 }).status === 0);

if (!fs.existsSync(VENOS)) {
    console.error('venos 실행 파일이 없습니다.  g++ -std=c++17 -O2 -o venos venos.cpp  먼저 실행하세요.');
    process.exit(2);
}

// docs/index.html 의 드롭다운 예제 — 방문자가 **제일 먼저 돌리는 코드**인데
// 레슨과 달리 아무 검사도 받고 있지 않았다. 레슨과 같은 취급을 한다.
function loadExamples() {
    const html = fs.readFileSync(path.join(ROOT, 'docs', 'index.html'), 'utf8');
    const m = html.match(/const EXAMPLES = (\{[\s\S]*?\n\});/);
    if (!m) { console.error('docs/index.html 에서 EXAMPLES 를 찾지 못했습니다.'); process.exit(2); }
    const ex = eval('(' + m[1] + ')');   // 우리 저장소 안의 데이터다
    // 레슨과 같은 모양으로 감싼다 (예제는 한 언어뿐이라 ko·en 에 같은 코드를 둔다)
    return Object.entries(ex).map(([id, code]) => ({ id, code: { ko: code }, desc: {} }));
}

const lessons = loadLessons();
const examples = loadExamples();
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'venos-lessons-'));

console.log(`레슨 ${lessons.length}개 + 플레이그라운드 예제 ${examples.length}개 점검\n`);

for (const lesson of lessons.concat(examples)) {
    for (const lang of Object.keys(lesson.code)) {
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

        // ---- 2) 세 방식이 같은 답을 내는가 ----
        // random 을 쓰는 레슨은 씨앗을 고정해야 인터프리터와 빌드본이 같은 수열을 돈다.
        const env = Object.assign({}, process.env, { VENOS_SEED: '20260917' });
        const again = spawnSync(VENOS, [file], { input: STDIN, encoding: 'utf8', timeout: 20000, env });
        const want = normalize((again.stdout || '') + (again.stderr || ''));
        const exe = file.replace(/\.my$/, '') + (process.platform === 'win32' ? '.exe' : '');
        const built = spawnSync(VENOS, ['build', file], { encoding: 'utf8', timeout: 120000 });
        if (!fs.existsSync(exe)) {
            fail(`${label}: build 가 실행 파일을 만들지 못했습니다\n      ${((built.stdout || '') + (built.stderr || '')).split('\n')[0]}`);
        } else {
            const ran = spawnSync(exe, [], { input: STDIN, encoding: 'utf8', timeout: 20000, env });
            const got = normalize((ran.stdout || '') + (ran.stderr || ''));
            if (got !== want) fail(`${label}: 빌드본의 출력이 인터프리터와 다릅니다\n${firstDiff(want, got)}`);
            fs.rmSync(exe, { force: true });
        }
        fs.rmSync(file.replace(/\.my$/, '') + '.cpp', { force: true });

        if (!PY_SKIP[lesson.id] && PY) {
            const py = file.replace(/\.my$/, '') + '.py';
            fs.rmSync(py, { force: true });
            const conv = spawnSync(VENOS, ['topython', file], { encoding: 'utf8', timeout: 20000 });
            if (!fs.existsSync(py)) {
                fail(`${label}: topython 이 레슨을 변환하지 못했습니다\n      ${((conv.stdout || '') + (conv.stderr || '')).split('\n')[0]}`);
            } else {
                const ran = spawnSync(PY, [py], { input: STDIN, encoding: 'utf8', timeout: 20000 });
                const got = normalize((ran.stdout || '') + (ran.stderr || ''));
                if (got !== want) fail(`${label}: 파이썬 변환본의 출력이 인터프리터와 다릅니다\n${firstDiff(want, got)}`);
                fs.rmSync(py, { force: true });
            }
        }

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
