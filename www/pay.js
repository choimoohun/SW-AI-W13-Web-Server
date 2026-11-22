/* pay.js — 예약 건에 대한 결제 화면. */
(function () {
  'use strict';

  var rid = new URLSearchParams(location.search).get('rid') || '';
  var client = localStorage.getItem('jg_client') || '';
  var $ = function (id) { return document.getElementById(id); };

  function showReservation() {
    if (!rid) {
      $('summary').textContent = '예약 번호가 없습니다.';
      $('payBtn').disabled = true;
      return;
    }
    fetch('/api/reservations?client=' + encodeURIComponent(client))
      .then(function (r) { return r.json(); })
      .then(function (data) {
        var found = (data.reservations || []).filter(function (r) { return r.id === rid; })[0];
        if (!found) {
          $('summary').textContent = '예약 ' + rid + '을 찾지 못했습니다.';
          return;
        }
        $('summary').innerHTML =
          '<div class="row"><span class="muted">예약 번호</span><span>' + rid + '</span></div>' +
          '<div class="row"><span class="muted">항공편</span><span>' + found.flight_id + '</span></div>' +
          '<div class="row"><span class="muted">승객</span><span>' + found.name + '</span></div>' +
          (found.cancelled ? '<div class="row"><span class="muted">상태</span><span>취소됨</span></div>' : '');
        if (found.cancelled) $('payBtn').disabled = true;
      });
    fetch('/api/payments/by-reservation?rid=' + encodeURIComponent(rid))
      .then(function (r) { return r.status === 404 ? null : r.json(); })
      .then(function (pay) {
        if (!pay) return;
        $('result').textContent = '이미 결제됨: ' + pay.id + ' · ' + pay.status;
        $('payBtn').disabled = true;
      });
  }

  $('payBtn').onclick = function () {
    if (!rid) return;
    $('payBtn').disabled = true;
    $('result').textContent = '결제 요청 중…';
    fetch('/api/payments/charge', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        reservation_id: rid,
        amount: 89000,
        method: $('method').value,
        idempotency_key: 'pay-' + rid
      })
    }).then(function (r) { return r.json(); }).then(function (pay) {
      if (pay.error) {
        $('result').textContent = '결제 실패: ' + (pay.message || pay.error);
        $('payBtn').disabled = false;
        return;
      }
      $('result').textContent = '결제 완료: ' + pay.id + ' · ' + pay.status;
    }).catch(function () {
      $('result').textContent = '결제 서비스에 연결할 수 없습니다.';
      $('payBtn').disabled = false;
    });
  };

  showReservation();
}());
