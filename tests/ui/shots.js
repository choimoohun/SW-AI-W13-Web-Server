/* make ui-smoke — 브라우저 UI 스모크 (Playwright).
 *
 * 순서:
 *   make demo-up → 예약 → 결제 → 내 예약 카드 초록 →
 *   make noti-down → 새 예약 결제 → 빨강 (2초 이내) →
 *   make noti-up → 같은 카드 2초 안에 초록 + 토스트 → make demo-down
 *
 * 각 단계는 새 브라우저 컨텍스트로 시작(기존 예약과 분리). 단계 D만
 * 단계 C의 카드/클라이언트를 다시 보기 위해 컨텍스트 C를 재사용한다.
 * 결과는 PASS/FAIL 로 출력하고, 하나라도 실패하면 exit 1.
 *
 * 실행: make ui-smoke   (Node + playwright-core 필요)
 * 환경: UI_SMOKE_CHROME 로 Chrome 실행 파일 경로를 바꿀 수 있다.
 */
'use strict';

const { chromium } = require('playwright-core');
const { execSync } = require('child_process');
const { existsSync } = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..', '..');
const URL = 'http://127.0.0.1:8085/flights.html';
const PORT = 8085;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

let globalFail = 0;

function pass(msg) { console.log('PASS  ' + msg); }
function failCase(msg) { console.log('FAIL  ' + msg); globalFail = 1; }
function check(cond, msg) { cond ? pass(msg) : failCase(msg); }
function sh(cmd) { return execSync(cmd, { cwd: ROOT, stdio: 'pipe' }).toString().trim(); }

function chromePath() {
  if (process.env.UI_SMOKE_CHROME && existsSync(process.env.UI_SMOKE_CHROME)) {
    return process.env.UI_SMOKE_CHROME;
  }
  const known = path.join(
    process.env.HOME || '', 'Library/Caches/ms-playwright/chromium-1248/chrome-mac-arm64/Google Chrome for Testing.app/Contents/MacOS/Google Chrome for Testing');
  if (existsSync(known)) return known;
  return null;   /* 없으면 Playwright 기본 헤드리스 셸로 실행 */
}

async function launchBrowser() {
  const exe = chromePath();
  if (exe) return chromium.launch({ executablePath: exe, args: ['--no-sandbox'] });
  return chromium.launch({ args: ['--no-sandbox'] });
}

function waitSrv(port, tries) {
  return new Promise((resolve) => {
    (function poll(t) {
      try {
        const r = execSync(
          `curl -s -o /dev/null --max-time 1 http://127.0.0.1:${port}/health`,
          { timeout: 2000 }).toString();
        if (r.trim().length > 0 && t >= tries) { resolve(true); return; }
        if (r.trim().length > 0) { resolve(true); return; }
      } catch (e) { /* 서버 미응답 */ }
      if (t >= tries) { resolve(false); return; }
      setTimeout(() => poll(t + 1), 200);
    })(0);
  });
}

function cardState(page) {
  return page.$$eval('#reservations .res', (cards) => cards.map((c) => ({
    id: c.getAttribute('data-rid'),
    cls: c.className,
    color: getComputedStyle(c).borderTopColor,
    text: (c.querySelector('.notice') || {}).textContent || '',
  })));
}

function cardGreen(page) {
  return cardState(page).then((cs) => cs.length > 0 && cs.every((c) =>
    c.cls.includes('notified') && c.color === 'rgb(63, 185, 80)'));
}

function cardRed(page) {
  return cardState(page).then((cs) => cs.length > 0 && cs.every((c) =>
    c.cls.includes('noti-down') && c.color === 'rgb(248, 81, 73)'));
}

function cardWaiting(page) {
  return cardState(page).then((cs) => cs.length > 0 && cs.every((c) =>
    c.cls.includes('waiting') && c.color === 'rgb(210, 153, 34)'));
}

async function waitGreen(page, ms) {
  const t0 = Date.now();
  while (Date.now() - t0 < ms) {
    if (await cardGreen(page)) return true;
    await sleep(200);
  }
  return false;
}

async function availFlightBtn(page) {
  return page.evaluate(() => {
    const btns = Array.from(document.querySelectorAll('#flights [data-flight]'));
    for (const b of btns) {
      const seats = (b.closest('.flight')?.querySelector('.seats') || {}).textContent || '';
      if (!/0석/.test(seats)) return b;
    }
    return null;
  });
}

/* 예약 → 결제 → 내 예약 카드 확보를 한 시나리오로 묶는다. */
async function stepReserve(page, clientId, wantGreen) {
  // flights.html → 좌석 있는 항공편 → 상세·예약 → 결제 → 내 예약
  await page.goto(URL);
  await page.evaluate((cid) => { localStorage.setItem('jg_client', cid); }, clientId);
  await page.reload();
  await page.click('#tabMine');

  const btn = await availFlightBtn(page);
  check(!!btn, '좌석 있는 항공편 선택');
  if (!btn) return null;

  await page.click('[data-flight]');
  await page.waitForSelector('#reserveBtn:not([disabled])');
  const name = 'ui-smoke-name-' + Date.now();
  const contact = '010-ui-' + Math.floor(Math.random() * 10000);
  await page.fill('#pName', name);
  await page.fill('#pContact', contact);
  await page.click('#reserveBtn');
  await page.waitForSelector('#payInlineBtn');
  const rid = await page.evaluate(() => {
    const a = document.querySelector('#payInlineBtn');
    return a ? a.getAttribute('href').split('rid=')[1] : null;
  });
  check(!!rid, '예약 완료 → 결제 패널 (rid=' + rid + ')');

  await page.goto('http://127.0.0.1:8085/pay.html?rid=' + rid);
  await page.click('#payBtn');
  await page.waitForSelector('#result');
  const result = await page.textContent('#result');
  check(result.includes('결제 완료'), '결제 완료 (pay.html, result=' + result.slice(0, 30) + '…)');

  await page.goto(URL);
  await page.click('#tabMine');
  await page.waitForSelector('.res[data-rid="' + rid + '"]');
  check(true, '내 예약에 새 카드 확보 (rid=' + rid + ')');

  const green = await waitGreen(page, wantGreen ? 2500 : 2500);
  check(green, '내 예약 카드 초록 (noti 정상)');
  if (green) {
    const stack = await page.textContent('#toastStack');
    check(stack.includes('예약 확정 알림 도착'), '토스트: 예약 확정 알림 도착');
  }
  return rid;
}

(async () => {
  const browser = await launchBrowser();
  let pageC = null;   // 단계 C와 D에서 같은 클라이언트로 '빨강→초록'을 확인

  try {
    check(await waitSrv(PORT, 60), 'make demo-up → 8085 기동');

    /* 1단계: 새 컨텍스트 → 예약 → 결제 → 내 예약 카드 초록 */
    {
      const cid = 'ui-smoke-A-' + Date.now().toString(36);
      const c = await browser.newContext({ viewport: { width: 1440, height: 900 } });
      const page = await c.newPage();
      page.on('console', (m) => { if (m.type() === 'error') console.log('[console.error]', m.text()); });
      await stepReserve(page, cid, true);
      await c.close();
    }

    /* 2단계: noti-down → 새 예약 결제 → 빨강 (2초 이내) */
    sh('make noti-down >/dev/null 2>&1');
    check(await waitSrv(PORT, 60), 'make noti-down → 알림 서비스 종료');
    {
      const cid = 'ui-smoke-C-' + Date.now().toString(36);
      const c = await browser.newContext({ viewport: { width: 1440, height: 900 } });
      const page = await c.newPage();
      page.on('console', (m) => { if (m.type() === 'error') console.log('[console.error]', m.text()); });
      pageC = page;
      const ridC = await stepReserve(page, cid, false);
      check(!!ridC, 'noti-down 단계: 새 예약 생성·결제 (rid=' + ridC + ')');
      const t0 = Date.now();
      let red = false;
      while (Date.now() - t0 < 2500) {
        await sleep(200);
        if (await cardRed(page)) { red = true; break; }
      }
      check(red, 'noti-down → 카드 빨강 (2초 이내)');
      await c.close();
    }

    /* 3단계: noti-up → 같은 카드(클라이언트 C) 2초 안에 초록 + 토스트 */
    sh('make noti-up >/dev/null 2>&1');
    check(await waitSrv(PORT, 60), 'make noti-up → 알림 서비스 재기동');
    {
      check(pageC, '단계 C 컨텍스트 유지 (같은 카드 확인용)');
      if (pageC) {
        await pageC.goto(URL);
        await pageC.click('#tabMine');
        const green = await waitGreen(pageC, 2500);
        check(green, 'noti-up → 같은 카드 2초 안에 초록');
        if (green) {
          const stack = await pageC.textContent('#toastStack');
          check(stack.includes('예약 확정 알림 도착'), '토스트: 예약 확정 알림 도착 (noti-up 후)');
        }
      }
    }
  } finally {
    sh('make demo-down >/dev/null 2>&1');
    await browser.close();
  }

  if (globalFail) {
    console.log('ui-smoke: FAILED');
    process.exit(1);
  }
  console.log('ui-smoke: all passed');
})();
