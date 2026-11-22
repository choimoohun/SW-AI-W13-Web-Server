/* flights.js — 항공권 예약 서비스 화면. 데모용 컨트롤 없음. */
(function () {
  'use strict';

  var client = localStorage.getItem('jg_client');
  if (!client) {
    client = 'c-' + Date.now().toString(36) + '-' + Math.random().toString(36).slice(2, 10);
    localStorage.setItem('jg_client', client);
  }
  var selected = null;

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
      '<div class="fields" style="margin-top:16px">' +
        '<label>승객 이름<input id="pName" maxlength="60" placeholder="홍길동"></label>' +
        '<label>연락처<input id="pContact" maxlength="60" placeholder="010-1234-5678"></label>' +
      '</div>' +
      '<button id="reserveBtn" class="primary" style="margin-top:14px"' +
        (f.seat_left <= 0 ? ' disabled' : '') + '>예약하기</button>';
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
        '<button id="payInlineBtn" class="primary">결제하기</button>' +
        '<a class="ghost" href="/pay.html?rid=' + encodeURIComponent(rid) + '">별도 결제 페이지</a>' +
      '</div>' +
      '<div id="payInlineResult" class="muted" style="margin-top:12px">결제 대기 중</div>';
    $('timeline').innerHTML = '';
    $('payInlineBtn').onclick = function () {
      $('payInlineBtn').disabled = true;
      $('payInlineResult').textContent = '결제 요청 중…';
      post('/api/payments/charge', {
        reservation_id: rid,
        amount: 89000,
        method: $('payMethod').value,
        idempotency_key: 'pay-' + rid
      }).then(function (pay) {
        if (pay.error) {
          $('payInlineResult').textContent = '결제 실패: ' + (pay.message || pay.error);
          $('payInlineBtn').disabled = false;
          return;
        }
        $('payInlineResult').textContent = '결제 완료: ' + pay.id + ' · ' + pay.status;
        loadReservations();
      }).catch(function () {
        $('payInlineResult').textContent = '결제 서비스에 연결할 수 없습니다.';
        $('payInlineBtn').disabled = false;
      });
    };
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

  function renderReservations(data) {
    var box = $('reservations');
    box.innerHTML = '';
    var list = (data.reservations || []).slice().reverse();
    if (list.length === 0) {
      box.innerHTML = '<div class="empty">결제된 예약이 없습니다.</div>';
      return;
    }
    var remaining = list.length;
    list.forEach(function (r) {
      fetch('/api/payments/by-reservation?rid=' + encodeURIComponent(r.id))
        .then(function (resp) { return resp.status === 404 ? null : resp.json(); })
        .then(function (pay) {
          if (!pay) return;   /* 결제 전 예약은 내 예약에 보여주지 않는다. */
          var el = document.createElement('div');
          el.className = 'res';
          el.innerHTML =
            '<div class="' + (r.cancelled ? 'cancelled' : '') + '">' +
              '<div class="code">' + r.flight_id + ' <span class="muted">' + r.id + '</span></div>' +
              '<div class="muted">' + r.name + (r.contact ? ' · ' + r.contact : '') + '</div>' +
              '<div class="muted">예약 시각 ' + time(r.created_epoch) +
                (r.cancelled ? ' · 취소됨' : '') + '</div>' +
              '<span class="muted">' +
                (pay.status === 'REFUNDED' ? '환불됨' : '결제 완료') +
                ' · ' + pay.amount + '원</span></div>' +
            (r.cancelled ? '' : '<button class="ghost" data-cancel="' + r.id + '">취소</button>');
          box.appendChild(el);
        })
        .catch(function () { /* 결제 서비스가 없으면 조용히 건너뛴다. */ })
        .then(function () {
          remaining--;
          if (remaining === 0 && box.children.length === 0) {
            box.innerHTML = '<div class="empty">결제된 예약이 없습니다.</div>';
          }
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
    var pay = e.target.getAttribute('data-pay');
    if (id) { selectFlight(id); return; }
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
}());
