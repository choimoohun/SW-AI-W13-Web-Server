/* flights.js — 항공권 예약 서비스 화면. 데모용 컨트롤 없음. */
(function () {
  'use strict';

  var client = localStorage.getItem('jg_client');
  if (!client) {
    client = 'c-' + Date.now().toString(36) + '-' + Math.random().toString(36).slice(2, 10);
    localStorage.setItem('jg_client', client);
  }
  var selected = null;

  /* ---------- 오른쪽 위 상태 영역 ---------- */
  var SHOWN_KEY = 'jg_shown_notices';   /* 이미 토스트로 띄운 알림 id (mq_id) */
  var NOTICE_MS = 5000;                 /* 알림 토스트를 띄워 두는 시간 */
  var POLL_MS = 2000;                   /* 알림·큐 통계 확인 주기 */

  function shownIds() {
    try { return JSON.parse(localStorage.getItem(SHOWN_KEY) || '[]'); }
    catch (e) { return []; }
  }
  function remember(id) {
    var ids = shownIds();
    if (ids.indexOf(id) === -1) ids.push(id);
    localStorage.setItem(SHOWN_KEY, JSON.stringify(ids));
  }

  /* 알림 문구에서 예약 번호를 되살려 쓴다: "🔔 예약 확정: 결제가 완료되어 예약 RS-0009 …" */
  function toastNotice(text) {
    var rid = '';
    var m = /예약\s+([A-Za-z0-9-]+)/.exec(text);
    if (m) rid = m[1];
    var head = rid ? ('🔔 예약 확정: ' + rid) : '🔔 예약 확정';
    var el = document.createElement('div');
    el.className = 'notice-toast';
    el.textContent = head + ' — ' + text;
    var stack = $('toastStack');
    if (!stack) return;
    stack.appendChild(el);
    window.setTimeout(function () {
      if (el.parentNode !== stack) return;   /* 이미 지워진 토스트 */
      el.style.opacity = '0';
      window.setTimeout(function () { el.remove(); }, 200);
    }, NOTICE_MS);
  }

  function setMqBadge(ready) {
    var b = $('mqBadge');
    if (!b) return;
    var n = ready > 0 ? ready : 0;
    b.textContent = '메시지 큐 대기 ' + n + '건';
    b.className = n > 0 ? 'hot' : '';   /* 0이면 회색(기본), 1 이상이면 주황색 */
  }

  /* 내 예약의 알림을 2초마다 확인한다. 토스트와 예약 카드 테두리를 같이 갱신한다.
     fetch는 502에서 예외를 던지지 않으므로 resp.status를 직접 검사한다. */
  function fetchNotice(rid) {
    return fetch('/api/notifications/by-reservation?rid=' + encodeURIComponent(rid))
      .then(function (resp) {
        if (!resp.ok) return { down: resp.status >= 500 };
        return resp.json();
      });
  }

  function cardFor(rid) {
    return document.querySelector('#reservations .res[data-rid="' + rid + '"]');
  }

  function pollNotices() {
    fetch('/api/reservations?client=' + encodeURIComponent(client))
      .then(function (r) { return r.json(); })
      .then(function (data) {
        var list = data.reservations || [];
        var seen = shownIds();
        list.forEach(function (res) {
          if (res.cancelled) return;   /* 취소된 예약은 알림 대기가 아니다. */
          fetchNotice(res.id)
            .then(function (out) {
              var card = cardFor(res.id);
              if (card) applyNotice(card, out);
              (out.notifications || []).forEach(function (n) {
                if (seen.indexOf(n.mq_id) !== -1) return;
                seen.push(n.mq_id);        /* 같은 주기라도 두 번 띄우지 않게 */
                remember(n.mq_id);
                toastNotice(n.text);
              });
            })
            .catch(function () {
              var card = cardFor(res.id);
              if (card) applyNotice(card, { down: true });
            });
        });
      })
      .catch(function () { /* 예약 목록을 못 가져와도 알림 동작은 계속 */ });

    fetch('/api/mq/stats?topic=reservation.paid')
      .then(function (r) { return r.json(); })
      .then(function (s) { setMqBadge(s.ready || 0); })
      .catch(function () { setMqBadge(0); });
  }

  function $(id) { return document.getElementById(id); }

  function toast(text, isErr) {
    var el = $('toast');
    el.textContent = text;
    el.className = isErr ? 'err' : '';
    el.style.display = 'block';
    window.setTimeout(function () { el.style.display = 'none'; }, 3000);
  }

  function time(epoch) {
    if (!epoch) return '-';
    return new Date(epoch * 1000).toLocaleString('ko-KR', {
      timeZone: 'Asia/Seoul', month: '2-digit', day: '2-digit',
      hour: '2-digit', minute: '2-digit', hour12: false
    });
  }

  function draftKey(flightId) { return 'jg_draft_' + flightId; }

  function saveDraft(flightId) {
    var name = $('pName');
    var contact = $('pContact');
    if (!name || !contact) return;
    localStorage.setItem(draftKey(flightId), JSON.stringify({
      name: name.value, contact: contact.value
    }));
  }

  function restoreDraft(flightId) {
    var name = $('pName');
    var contact = $('pContact');
    if (!name || !contact) return;
    try {
      var saved = JSON.parse(localStorage.getItem(draftKey(flightId)) || '{}');
      name.value = saved.name || '';
      contact.value = saved.contact || '';
    } catch (e) {
      name.value = '';
      contact.value = '';
    }
  }

  function post(url, body) {
    return fetch(url, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body)
    }).then(function (r) { return r.json(); });
  }

  function statusText(f) {
    var s = f.status;
    if (f.delay_min > 0) s += ' · ' + f.delay_min + '분 지연';
    return s;
  }

  function refundPolicy(leftSec) {
    if (leftSec <= 0) return { rate: 0, label: '출발됨', open: false };
    if (leftSec <= 86400) return { rate: 0, label: '환불 신청 마감', open: false };
    if (leftSec <= 2 * 86400) return { rate: 50, label: '50% 환불', open: true };
    if (leftSec <= 5 * 86400) return { rate: 80, label: '80% 환불', open: true };
    return { rate: 100, label: '100% 환불', open: true };
  }

  function renderFlights(data) {
    var box = $('flights');
    var list = data.flights || [];
    box.innerHTML = '';
    if (list.length === 0) {
      box.innerHTML = '<div class="empty">조건에 맞는 항공편이 없습니다.</div>';
      return;
    }
    list.forEach(function (f) {
      var el = document.createElement('div');
      el.className = 'flight';
      el.innerHTML =
        '<div><div class="code">' + f.id + '</div>' +
          '<div class="route">' + f.from + ' → ' + f.to + '</div>' +
          '<div class="time">출발 ' + time(f.depart_epoch) +
            ' · 도착 ' + time(f.arrive_epoch) + '</div>' +
          '<span class="status' + (f.delay_min > 0 ? ' delayed' : '') + '">' +
            statusText(f) + '</span></div>' +
        '<div class="right"><div class="seats' + (f.seat_left <= 1 ? ' low' : '') + '">' +
          '잔여 ' + f.seat_left + '석</div>' +
          '<button class="ghost" data-flight="' + f.id + '">상세 · 예약</button></div>';
      box.appendChild(el);
    });
  }

  function loadFlights() {
    var q = [];
    if ($('from').value) q.push('from=' + encodeURIComponent($('from').value));
    if ($('to').value) q.push('to=' + encodeURIComponent($('to').value));
    fetch('/api/flights' + (q.length ? '?' + q.join('&') : ''))
      .then(function (r) { return r.json(); })
      .then(renderFlights);
  }

  function renderDetail(f) {
    $('detailTitle').textContent = f.id + ' 상세';
    $('detailBody').innerHTML =
      '<div class="code">' + f.from + ' → ' + f.to + '</div>' +
      '<div class="muted">출발 ' + time(f.depart_epoch) + ' · 도착 ' + time(f.arrive_epoch) + '</div>' +
      '<div class="muted">운항 상태: ' + statusText(f) + '</div>' +
      '<div class="muted">잔여 좌석: ' + f.seat_left + ' / ' + f.seat_total + '</div>' +
      '<div id="airportInfo" class="airport-info"></div>' +
      '<div class="fields" style="margin-top:16px">' +
        '<label>승객 이름<input id="pName" maxlength="60" placeholder="홍길동"></label>' +
        '<label>연락처<input id="pContact" maxlength="60" placeholder="010-1234-5678"></label>' +
      '</div>' +
      '<button id="reserveBtn" class="primary" style="margin-top:14px"' +
        (f.seat_left <= 0 ? ' disabled' : '') + '>예약하기</button>';
    loadAirportInfo(f);
    restoreDraft(f.id);
    $('pName').oninput = function () { saveDraft(f.id); };
    $('pContact').oninput = function () { saveDraft(f.id); };
    $('reserveBtn').onclick = function () {
      var name = $('pName').value.trim();
      var contact = $('pContact').value.trim();
      if (!name) { toast('승객 이름을 입력해 주세요.', true); return; }
      post('/api/flights/' + f.id + '/reserve', {
        name: name, contact: contact, client: client
      }).then(function (r) {
        if (r.error) { toast(r.message || r.error, true); return; }
        localStorage.removeItem(draftKey(f.id));
        toast('예약 완료. 오른쪽에서 결제를 진행해 주세요.');
        loadFlights();
        renderPaymentPanel(r.reservation.id, f, r.reservation);
      });
    };
  }

  // 공항 정보는 별도 백엔드(공항 정보 서비스 2대)가 준다. 게이트웨이가 Round-Robin으로 나눠 보내므로
  // 응답한 서버(A/B)를 같이 표시한다. 그 서비스가 죽어도 이 칸만 안내 문구로 바뀌고 예약은 그대로 된다.
  function loadAirportInfo(f) {
    var box = $('airportInfo');
    if (!box) return;
    box.innerHTML = '<div class="muted">공항 정보를 불러오는 중…</div>';
    function one(label, code) {
      return fetch('/api/airports/' + encodeURIComponent(code))
        .then(function (r) {
          if (!r.ok) throw new Error(String(r.status));
          return r.json();
        })
        .then(function (a) {
          return '<div class="airport-row"><b>' + label + ' · ' + a.name + '</b>' +
            '<span>' + a.city + ' · ' + a.terminal + ' · 혼잡도 ' + a.congestion +
            ' <em class="served-by">서버 ' + a.server + '</em></span></div>';
        })
        .catch(function (e) {
          return '<div class="airport-row down"><b>' + label + ' · ' + code + '</b>' +
            '<span>공항 정보를 잠시 불러올 수 없습니다 (' + e.message +
            '). 예약과 결제는 정상 이용 가능합니다.</span></div>';
        });
    }
    Promise.all([one('출발', f.from), one('도착', f.to)]).then(function (rows) {
      if ($('airportInfo') === box) box.innerHTML = rows.join('');
    });
  }

  function renderPaymentPanel(rid, f, reservation) {
    $('detailTitle').textContent = '결제 · ' + rid;
    $('detailBody').innerHTML =
      '<div class="code">' + f.id + ' ' + f.from + ' → ' + f.to + '</div>' +
      '<div class="muted">' + reservation.name +
        (reservation.contact ? ' · ' + reservation.contact : '') + '</div>' +
      '<div style="font-size:26px;font-weight:800;color:#7ee787;margin:14px 0">89,000원</div>' +
      '<div class="fields">' +
        '<label>결제 수단<select id="payMethod">' +
          '<option value="card">신용카드</option>' +
          '<option value="bank">무통장 입금</option>' +
        '</select></label>' +
        '<a id="payInlineBtn" class="primary" href="/pay.html?rid=' +
          encodeURIComponent(rid) + '">결제하기</a>' +
      '</div>' +
      '<div id="payInlineResult" class="muted" style="margin-top:12px">결제 페이지로 이동합니다.</div>';
    $('timeline').innerHTML = '';
  }

  function renderTimeline(data) {
    var box = $('timeline');
    box.innerHTML = '<h2 style="margin-top:20px">운항 이력</h2>';
    (data.events || []).slice().reverse().forEach(function (e) {
      var el = document.createElement('div');
      el.className = 'timeline-item';
      el.innerHTML = '<b>' + e.status + '</b><span>' + time(e.ts) + ' · ' + e.note + '</span>';
      box.appendChild(el);
    });
  }

  function selectFlight(id) {
    selected = id;
    fetch('/api/flights/' + id)
      .then(function (r) { return r.json(); })
      .then(function (f) { if (!f.error) renderDetail(f); });
    fetch('/api/flights/' + id + '/timeline')
      .then(function (r) { return r.json(); })
      .then(renderTimeline);
  }

  /* 결제된 예약의 알림 상태를 카드 테두리·한 줄 문구로 나타낸다.
     - 알림 API가 5xx(502) → 빨간 테두리: 결제는 됐고 메시지 큐에 대기 중
     - 알림 있음             → 초록 테두리: 예약 확정 알림 도착 + 시각
     - 결제됐고 알림 없음    → 주황 테두리: 메시지 큐 처리 전
     상태가 같으면 DOM을 다시 만들지 않는다. */
  function applyNotice(card, out) {
    var state;
    var text;
    if (out && out.down) {
      state = 'noti-down';
      text = '결제 완료 · 알림 서비스 중지됨 (메시지 큐에 대기 중)';
    } else {
      var list = (out && out.notifications) || [];
      if (list.length === 0) {
        state = 'waiting';
        text = '알림 대기 중 (메시지 큐 처리 전)';
      } else {
        state = 'notified';
        text = '🔔 예약 확정 알림 도착 · ' + time(list[list.length - 1].time);
      }
    }
    if (card.getAttribute('data-notice') === state) return;
    card.setAttribute('data-notice', state);
    card.classList.remove('waiting', 'noti-down', 'notified');
    card.classList.add(state);
    var prev = card.querySelector('.notice');   /* 이전 상태의 문구를 덮어쓴다. */
    if (prev) prev.remove();
    var d = document.createElement('div');
    d.className = 'notice';
    d.textContent = text;
    card.appendChild(d);
  }

  function renderReservations(data) {
    var box = $('reservations');
    box.innerHTML = '';
    var list = (data.reservations || []).slice().reverse();
    if (list.length === 0) {
      box.innerHTML = '<div class="empty">결제된 예약이 없습니다.</div>';
      return;
    }
    fetch('/api/ops/clock').then(function (r) { return r.json(); }).then(function (clock) {
      var now = clock.now || (Date.now() / 1000);
      var remaining = list.length;
      function done() {
        remaining--;
        if (remaining === 0 && box.children.length === 0) {
          box.innerHTML = '<div class="empty">결제된 예약이 없습니다.</div>';
        }
      }
      list.forEach(function (r) {
        fetch('/api/payments/by-reservation?rid=' + encodeURIComponent(r.id))
          .then(function (resp) { return resp.status === 404 ? null : resp.json(); })
          .then(function (pay) {
            if (!pay) { done(); return; }   /* 결제 전 예약은 숨긴다. */
            return fetch('/api/flights/' + encodeURIComponent(r.flight_id))
              .then(function (resp) { return resp.json(); })
              .then(function (f) {
                var left = (f.depart_epoch || 0) - now;
                var pol = refundPolicy(left);
                var refundable = pol.open && !r.cancelled && pay.status !== 'REFUNDED';
                var days = Math.floor(left / 86400);
                var el = document.createElement('div');
                el.className = 'res';
                el.setAttribute('data-rid', r.id);
                el.innerHTML =
                  '<div class="' + (r.cancelled ? 'cancelled' : '') + '">' +
                    '<div class="code">' + r.flight_id + ' <span class="muted">' + r.id + '</span></div>' +
                    '<div class="muted">' + r.name + (r.contact ? ' · ' + r.contact : '') + '</div>' +
                    '<div class="muted">출발 ' + time(f.depart_epoch) +
                      ' · ' + (left > 0 ? days + '일 전' : '출발됨') + '</div>' +
                    '<span class="muted">' +
                      (pay.status === 'REFUNDED' ? '환불됨' : '결제 완료') +
                      ' · ' + pay.amount + '원 · ' + pol.label + '</span></div>' +
                  (r.cancelled ? '' :
                    (refundable
                      ? '<button class="ghost" data-refund="' + pay.id + '">환불 요청</button>'
                      : '') +
                    '<button class="ghost" data-cancel="' + r.id + '">취소</button>');
                box.appendChild(el);
                done();
              });
          })
          .catch(done);
      });
    });
  }

  function loadReservations() {
    fetch('/api/reservations?client=' + encodeURIComponent(client))
      .then(function (r) { return r.json(); })
      .then(renderReservations);
  }

  document.addEventListener('click', function (e) {
    var id = e.target.getAttribute('data-flight');
    var cancel = e.target.getAttribute('data-cancel');
    var refund = e.target.getAttribute('data-refund');
    var pay = e.target.getAttribute('data-pay');
    if (id) { selectFlight(id); return; }
    if (refund) {
      post('/api/payments/' + refund + '/refund', {}).then(function (r) {
        if (r.error) { toast(r.message || r.error, true); return; }
        toast('환불이 완료되었습니다.');
        loadReservations();
      });
      return;
    }
    if (pay) {
      window.location.href = '/pay.html?rid=' + encodeURIComponent(pay);
      return;
    }
    if (cancel) {
      post('/api/reservations/' + cancel + '/cancel', { client: client })
        .then(function (r) {
          if (r.error) { toast(r.message || r.error, true); return; }
          toast('예약이 취소되었습니다.');
          loadReservations();
          loadFlights();
          if (selected) selectFlight(selected);
        });
      return;
    }
    if (e.target.id === 'searchBtn') { loadFlights(); return; }
    if (e.target.id === 'tabMine') {
      $('tabMine').className = 'active';
      $('tabSearch').className = '';
      $('viewSearch').style.display = 'none';
      $('viewMine').style.display = '';
      loadReservations();
      return;
    }
    if (e.target.id === 'tabSearch') {
      $('tabSearch').className = 'active';
      $('tabMine').className = '';
      $('viewSearch').style.display = '';
      $('viewMine').style.display = 'none';
    }
  });

  loadFlights();
  window.setInterval(function () {
    if ($('viewSearch').style.display !== 'none') loadFlights();
    else loadReservations();
  }, 15000);

  /* 알림·메시지 큐 상태는 화면과 별개로 2초마다 갱신한다. */
  pollNotices();
  window.setInterval(pollNotices, POLL_MS);
}());
