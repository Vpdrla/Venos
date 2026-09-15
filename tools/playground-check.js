#!/usr/bin/env node
// 플레이그라운드(docs/)를 진짜 브라우저로 열어 확인하는 스크립트.
//
//   node tools/playground-check.js          # 지금 브라우저 그대로
//   node tools/playground-check.js --future # "미래 Chrome" 흉내 (아래 설명)
//
// 왜 흉내가 필요한가:
//   emscripten 은 16바이트가 넘는 문자열만 TextDecoder.decode(HEAPU8.subarray(...)) 로 푼다.
//   최신 Chrome 은 성장 가능한 wasm 힙을 resizable ArrayBuffer 로 주는데 TextDecoder 는 그런
//   버퍼를 거부한다 → 긴 input 프롬프트에서 다음 에러로 죽는다:
//     TypeError: Failed to execute 'decode' on 'TextDecoder':
//               The provided ArrayBuffer value must not be resizable
//   이 조건은 브라우저 버전에 달려 있어서 손으로 만들 수 없다. --future 는 wasm 힙(64MB 이상
//   버퍼) 위의 뷰가 들어오면 같은 TypeError 를 던지도록 TextDecoder 를 감싸 그 상황을 재현한다.
//   EM_JS 에서 힙 문자열을 만질 때는 이걸 꼭 통과시킬 것.
//
// CI 의 playground-wasm 잡이 이걸 돌린다 (Playwright 를 거기서 설치한다).
// --future 만 CI 에 없다 — 손으로 한 번 돌려 볼 것.

const http = require('http');
const fs = require('fs');
const path = require('path');

const ROOT = path.join(__dirname, '..');
const DOCS = path.join(ROOT, 'docs');
const FUTURE = process.argv.includes('--future');
const PORT = 8731;

function loadPlaywright() {
  for (const c of ['playwright', '/opt/node22/lib/node_modules/playwright']) {
    try { return require(c); } catch (e) { /* 다음 후보 */ }
  }
  console.error('playwright 를 찾을 수 없습니다.  npm i -g playwright  후 다시 실행하세요.');
  process.exit(2);
}

const TYPES = {
  '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8',
  '.wasm': 'application/wasm', '.gif': 'image/gif',
};
function serve() {
  return new Promise(resolve => {
    const srv = http.createServer((req, res) => {
      const rel = decodeURIComponent(req.url.split('?')[0]);
      const file = path.join(DOCS, rel === '/' ? 'index.html' : rel);
      if (!file.startsWith(DOCS) || !fs.existsSync(file) || fs.statSync(file).isDirectory()) {
        res.writeHead(404); res.end('not found'); return;
      }
      res.writeHead(200, { 'Content-Type': TYPES[path.extname(file)] || 'application/octet-stream' });
      fs.createReadStream(file).pipe(res);
    });
    srv.listen(PORT, '127.0.0.1', () => resolve(srv));
  });
}

const LESSONS = require(path.join(DOCS, 'lessons.js'));
const lesson = (id, lang) => {
  const c = LESSONS.find(l => l.id === id).code;
  return c[lang || 'ko'] || c.ko;
};

// 실제로 학생이 밟는 경로들. 프롬프트 길이가 16바이트를 넘는 것들이 위험 구간이다.
// paintedAtInput: 입력을 기다리는 그 순간 출력창에 이미 보여야 하는 글자
//   (예전엔 실행이 동기라 화면이 안 칠해져서, 무엇을 묻는지 알 수가 없었다)
const CHECKS = [
  { name: '짧은 프롬프트 (16바이트 이하)', code: 'let x = input "> "\nprint "got", x\n',
    answer: '미르', expect: 'got 미르' },
  { name: '긴 프롬프트 (영문 28바이트)', code: 'let x = input "What is your hero\'s name? > "\nprint "got", x\n',
    answer: 'Mir', expect: 'got Mir' },
  { name: '긴 프롬프트 (한글 21바이트)', code: 'let x = input "이름이 뭐예요? "\nprint "got", x\n',
    answer: '미르', expect: 'got 미르' },
  { name: '입력 기다릴 때 이미 출력이 보임',
    code: 'print "===== 던전 ====="\nprint "[1] 새 게임  [2] 불러오기"\nlet x = input "> "\nprint "골랐다:", x\n',
    answer: '1', expect: '골랐다: 1', paintedAtInput: '[1] 새 게임' },
  { name: '레슨 3 (input)', code: lesson('input'), answer: '미르', expect: '미르님' },
  { name: '긴 출력 · 보간', code: 'let a = "열여섯 바이트를 훌쩍 넘는 아주 긴 한글 문자열입니다"\nprint "값: {a}"\n',
    expect: '값: 열여섯' },
  // 브라우저 호출 스택은 네이티브보다 얕다. 한도(웹 400) 안쪽은 돌아야 하고, 넘어가면
  // V8 의 RangeError 가 아니라 Venos 의 한국어 메시지가 나와야 한다.
  { name: '한도 안쪽 재귀 (190)',
    code: 'func 합(n) { if n <= 0 { return 0 }  return n + 합(n - 1) }\nprint 합(190)\n',
    expect: '18145' },
  { name: '한도를 넘으면 Venos 메시지',
    code: 'func 끝없이(n) { return 끝없이(n + 1) }\nprint 끝없이(1)\n',
    expect: '함수 호출이 너무 깊습니다', expectError: true },
  // 한도를 넘긴 바로 다음 실행 — 깊이와 호출 프레임이 남아 있으면 여기서 드러난다
  { name: '넘긴 다음 실행도 멀쩡',
    code: 'func 합(n) { if n <= 0 { return 0 }  return n + 합(n - 1) }\nprint 합(150)\n',
    expect: '11325' },
  // 아래 셋은 "브라우저가 최신 wasm 을 들고 있는가"를 본다. 소스 해시 검사는 커밋된
  // 파일이 소스와 맞는지만 보지, 그 안에 기능이 실제로 들어갔는지는 못 본다.
  { name: '별 찍기 ("*" * n)', code: 'for i = 1 to 3 { print "*" * i }\nprint "-" * 5\n',
    expect: '***\n-----' },
  { name: '오타 제안', code: 'let 이름 = "미르"\nprint 이릅\n',
    expect: "혹시 '이름'?", expectError: true },
  { name: '에러의 호출 경로', code: 'func 안쪽(xs) { return xs[9] }\nfunc 바깥(xs) { return 안쪽(xs) }\nprint 바깥([1])\n',
    expect: '부른 순서: 바깥', expectError: true },
  // 브라우저 스택은 32MB(링크 시 TOTAL_STACK), 네이티브는 128MB 다. 재귀 한도 2000 에
  // 닿기 전에 스택이 먼저 터지면 학생에게는 탭이 멈춘 것으로 보인다 — 한도 직전까지 가 본다.
  { name: '레슨 10 (classes) 실행', code: lesson('classes'), expect: '멍멍' },
];

(async () => {
  const { chromium } = loadPlaywright();
  const srv = await serve();
  const exe = process.env.PLAYWRIGHT_CHROMIUM
           || '/opt/pw-browsers/chromium-1194/chrome-linux/chrome';
  const browser = await chromium.launch(fs.existsSync(exe) ? { executablePath: exe } : {});
  const context = await browser.newContext();
  // 🔗 Share 는 클립보드에 링크를 쓴다 — 왕복을 검사하려면 읽을 수 있어야 한다
  await context.grantPermissions(['clipboard-read', 'clipboard-write'],
                                 { origin: `http://127.0.0.1:${PORT}` }).catch(() => {});
  const page = await context.newPage();

  if (FUTURE) {
    await page.addInitScript(() => {
      const orig = TextDecoder.prototype.decode;
      TextDecoder.prototype.decode = function (input, opts) {
        if (input && input.buffer && input.buffer.byteLength >= 64 * 1024 * 1024) {
          throw new TypeError("Failed to execute 'decode' on 'TextDecoder': " +
                              'The provided ArrayBuffer value must not be resizable');
        }
        return orig.call(this, input, opts);
      };
    });
  }

  const jsErrors = [];
  page.on('pageerror', e => jsErrors.push(String(e.message)));
  page.on('console', m => { if (m.type() === 'error') jsErrors.push(m.text()); });
  page.on('dialog', async d => { await d.accept(); });   // askThenSet 의 confirm()

  await page.goto(`http://127.0.0.1:${PORT}/`, { waitUntil: 'networkidle' });
  await page.waitForSelector('#runBtn:not([disabled])', { timeout: 60000 });

  console.log(`플레이그라운드 확인${FUTURE ? '  [--future: 미래 Chrome 흉내]' : ''}\n`);
  let bad = 0;

  // 실행이 끝날 때까지 돌면서, 입력줄이 뜨면 답을 넣어 준다.
  // 입력을 기다리는 순간의 출력 내용도 같이 돌려준다 (화면이 칠해졌는지 확인용).
  const runAndRead = async (sel, answer) => {
    let atInput = null;
    await page.click(sel);
    const deadline = Date.now() + 30000;
    while (Date.now() < deadline) {
      const st = await page.evaluate(() => ({
        out: document.querySelector('#output').textContent,
        waiting: !document.querySelector('#inputLine').hidden,
        running: document.querySelector('#runBtn').disabled,
      }));
      if (st.waiting) {
        if (atInput === null) atInput = st.out;
        await page.fill('#inputBox', answer ?? '');
        await page.press('#inputBox', 'Enter');
      } else if (!st.running &&
                 (st.out.includes('=== done ===') || st.out.includes('!!') || st.out.includes('print('))) {
        break;
      }
      await page.waitForTimeout(80);
    }
    return { out: (await page.$eval('#output', e => e.textContent)).trim(), atInput };
  };

  for (const c of CHECKS) {
    await page.fill('#editor', c.code);
    const { out, atInput } = await runAndRead('#runBtn', c.answer);
    let ok = out.includes(c.expect) && (c.expectError || !out.includes('!!'));
    if (ok && c.paintedAtInput && !(atInput || '').includes(c.paintedAtInput)) {
      ok = false;
      console.log(`✗ ${c.name}  — 입력을 기다리는데 출력이 화면에 없음`);
      console.log('   입력 시점 출력: ' + JSON.stringify(atInput));
    } else if (!ok) {
      console.log(`✗ ${c.name}`);
      console.log('   ' + out.split('\n').join('\n   '));
    }
    if (ok) console.log(`✓ ${c.name}`); else bad++;
  }

  // 🐍 Python 버튼
  await page.fill('#editor', 'let 이름 = "미르"\nprint "안녕, {이름}! 반가워요 정말로"\n');
  const py = (await runAndRead('#pyBtn')).out;
  if (py.includes('print(f"') && !py.includes('!!')) console.log('✓ 🐍 Python 버튼');
  else { bad++; console.log('✗ 🐍 Python 버튼'); console.log('   ' + py.split('\n').join('\n   ')); }

  // 앞 실행이 죽어도 다음 실행 첫 줄이 깨끗한가 (개행 없는 프롬프트 찌꺼기)
  await page.fill('#editor', 'let x = input "이름이 뭐예요? "\nprint x\n');
  await runAndRead('#runBtn', '미르');
  await page.fill('#editor', 'print "첫 줄"\n');
  const after = (await runAndRead('#runBtn')).out;
  if (after.split('\n')[0].trim() === '첫 줄') console.log('✓ 이전 실행 찌꺼기 없음');
  else { bad++; console.log('✗ 이전 실행 찌꺼기가 첫 줄에 붙음'); console.log('   ' + after.split('\n')[0]); }

  // 🔗 Share — 선생님이 시작 코드를 나눠 주는 통로다. 압축·base64url 을 거쳐 돌아오는
  // 왕복이 깨지면 링크를 받은 학생이 빈 편집기를 보게 된다.
  {
    const 원본 = 'let 점수 = [88, 92, 79]\nfunc 평균(xs) {\n    let s = 0\n    for x in xs { s += x }\n    return s / len(xs)\n}\nprint "평균: {평균(점수)}"\n';
    await page.fill('#editor', 원본);
    await page.click('#shareBtn');
    // 버튼이 반응은 했는지 (클립보드에 복사했거나 주소창에 남겼거나)
    await page.waitForFunction(() => document.getElementById('shareMsg').textContent !== '',
                               null, { timeout: 5000 }).catch(() => {});
    const 안내 = await page.$eval('#shareMsg', e => e.textContent);
    if (!안내) { bad++; console.log('✗ 🔗 Share 버튼이 아무 말도 안 함'); }
    // 실제 링크는 클립보드에서 (막혀 있으면 주소창, 그것도 아니면 직접 만들어 본다).
    // 어느 쪽이든 검사하려는 것은 압축·base64url 왕복이다.
    let hash = await page.evaluate(async (code) => {
      if (location.hash.startsWith('#code=')) return location.hash;
      try {
        const t = await navigator.clipboard.readText();
        const i = t.indexOf('#code=');
        if (i >= 0) return t.slice(i);
      } catch (e) { /* 클립보드가 막힌 환경 */ }
      return '#code=' + await encodeCode(code);
    }, 원본);
    if (!hash.startsWith('#code=')) {
      bad++; console.log('✗ 🔗 Share — 링크를 얻지 못함: ' + JSON.stringify(hash));
    } else {
      await page.goto(`http://127.0.0.1:${PORT}/${hash}`, { waitUntil: 'networkidle' });
      await page.waitForSelector('#runBtn:not([disabled])', { timeout: 60000 });
      const 돌아온것 = await page.$eval('#editor', e => e.value);
      if (돌아온것.trim() === 원본.trim()) console.log('✓ 🔗 Share 링크 왕복');
      else {
        bad++;
        console.log('✗ 🔗 Share 링크 왕복 — 코드가 달라짐');
        console.log('   ' + JSON.stringify(돌아온것.slice(0, 120)));
      }
      // 뒤 검사들을 위해 깨끗한 페이지로 돌아간다
      await page.goto(`http://127.0.0.1:${PORT}/`, { waitUntil: 'networkidle' });
      await page.waitForSelector('#runBtn:not([disabled])', { timeout: 60000 });
    }
  }

  // 레슨 언어 토글이 코드까지 바꾸는가 (손대지 않은 시작 코드일 때만)
  const lists = LESSONS.find(l => l.id === 'lists').code;
  await page.goto(`http://127.0.0.1:${PORT}/?lessoncheck=1#lesson=lists`, { waitUntil: 'networkidle' });
  await page.waitForFunction(() => document.querySelector('#editor').value.includes('push('), { timeout: 20000 });
  const before = await page.$eval('#editor', e => e.value);
  await page.click('#lessonLang');
  await page.waitForTimeout(300);
  const afterCode = await page.$eval('#editor', e => e.value);
  const pair = [lists.ko.trim(), lists.en.trim()];
  if (before.trim() !== afterCode.trim()
      && pair.includes(before.trim()) && pair.includes(afterCode.trim())) {
    console.log('✓ 레슨 언어 토글이 코드까지 바꿈');
  } else {
    bad++;
    console.log('✗ 레슨 언어 토글이 코드를 안 바꿈');
    console.log('   before: ' + before.split('\n')[0]);
    console.log('   after : ' + afterCode.split('\n')[0]);
  }

  if (jsErrors.length) { bad++; console.log('\n✗ JS 에러:\n   ' + jsErrors.join('\n   ')); }
  else console.log('✓ JS 에러 없음');

  await browser.close();
  srv.close();
  console.log(bad ? `\n실패 ${bad}건` : '\n전부 통과');
  process.exit(bad ? 1 : 0);
})().catch(e => { console.error('하네스 자체가 실패:', e); process.exit(2); });
