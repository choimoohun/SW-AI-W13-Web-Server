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
    $('reserveBtn').onclick = function () {
      var name = $('pName').value.trim();
      var contact = $('pContact').value.trim();
      if (!name) { toast('승객 이름을 입력해 주세요.', true); return; }
      post('/api/flights/' + f.id + '/reserve', {
        name: name, contact: contact, client: client
      }).then(function (r) {
        if (r.error) { toast(r.message || r.error, true); return; }
        toast('예약 완료: ' + r.reservation.id);
        loadFlights();
        selectFlight(f.id);
        loadReservations();
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
      box.innerHTML = '<div class="empty">예약 내역이 없습니다.</div>';
      return;
    }
    list.forEach(function (r) {
      var el = document.createElement('div');
      el.className = 'res';
      el.innerHTML =
        '<div class="' + (r.cancelled ? 'cancelled' : '') + '">' +
          '<div class="code">' + r.flight_id + ' <span class="muted">' + r.id + '</span></div>' +
          '<div class="muted">' + r.name + (r.contact ? ' · ' + r.contact : '') + '</div>' +
          '<div class="muted">예약 시각 ' + time(r.created_epoch) +
            (r.cancelled ? ' · 취소됨' : '') + '</div>' +
          '<span class="muted" data-pay-status="' + r.id + '">' +
            (r.cancelled ? '' : '결제 확인 중…') + '</span></div>' +
        (r.cancelled ? '' : '<button class="ghost" data-cancel="' + r.id + '">취소</button>');
      box.appendChild(el);
      if (r.cancelled) return;
      fetch('/api/payments/by-reservation?rid=' + encodeURIComponent(r.id))
        .then(function (resp) { return resp.status === 404 ? null : resp.json(); })
        .then(function (pay) {
          var slot = box.querySelector('[data-pay-status="' + r.id + '"]');
          if (!slot) return;
          if (pay) {
            slot.textContent = (pay.status === 'REFUNDED' ? '환불됨' : '결제 완료') +
              ' · ' + pay.amount + '원';
          } else {
            slot.innerHTML = '<button class="ghost" data-pay="' + r.id + '">결제하기</button>';
          }
        })
        .catch(function () {
          var slot = box.querySelector('[data-pay-status="' + r.id + '"]');
          if (slot) slot.textContent = '결제 서비스 확인 불가';
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
      post('/api/payments/charge', {
        reservation_id: pay,
        amount: 89000,
        method: 'card',
        idempotency_key: 'pay-' + pay
      }).then(function (r) {
        if (r.error) { toast(r.message || r.error, true); return; }
        toast('결제 완료: ' + r.id);
        loadReservations();
      });
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
