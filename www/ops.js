/* ops.js — 출발/도착 시간 추적 운영 화면. 시각은 서버 /api/ops/clock을 따른다. */
(function () {
  'use strict';

  var selected = null;
  var serverNow = 0;
  var mode = 'real';
  var speed = 1;

  function $(id) { return document.getElementById(id); }

  function fmt(epoch) {
    if (!epoch) return '-';
    return new Date(epoch * 1000).toLocaleString('ko-KR', {
      timeZone: 'Asia/Seoul', month: '2-digit', day: '2-digit',
      hour: '2-digit', minute: '2-digit', hour12: false
    });
  }

  function countdown(epoch) {
    if (!epoch) return '-';
    var d = epoch - serverNow;
    if (d <= 0) return '경과';
    var m = Math.floor(d / 60);
    if (m < 60) return m + '분';
    return Math.floor(m / 60) + '시간 ' + (m % 60) + '분';
  }

  function post(url, body) {
    return fetch(url, {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body)
    }).then(function (r) { return r.json(); });
  }

  function toast(text, isErr) {
    var el = $('toast');
    el.textContent = text;
    el.className = isErr ? 'err' : '';
    el.style.display = 'block';
    window.setTimeout(function () { el.style.display = 'none'; }, 2500);
  }

  function renderClock() {
    var tag = mode === 'sim' ? ('시뮬레이션 ×' + speed) : '실제 시간';
    $('clock').textContent = tag + ' · 서버 시각 ' + fmt(serverNow) +
      ' (' + serverNow + ')';
  }

  function renderRows(data) {
    var body = $('rows');
    body.innerHTML = '';
    (data.flights || []).forEach(function (f) {
      var tr = document.createElement('tr');
      if (f.id === selected) tr.style.background = '#1b2a42';
      tr.innerHTML =
        '<td class="code">' + f.id + '</td>' +
        '<td>' + f.from + ' → ' + f.to + '</td>' +
        '<td><span class="status ' + f.status + '">' + f.status + '</span></td>' +
        '<td>' + fmt(f.depart_epoch) + '<br><span class="muted">' + countdown(f.depart_epoch) + '</span></td>' +
        '<td class="' + (f.actual_depart_epoch ? '' : 'muted') + '">' + fmt(f.actual_depart_epoch) + '</td>' +
        '<td>' + fmt(f.arrive_epoch) + '<br><span class="muted">' + countdown(f.arrive_epoch) + '</span></td>' +
        '<td class="' + (f.actual_arrive_epoch ? '' : 'muted') + '">' + fmt(f.actual_arrive_epoch) + '</td>' +
        '<td class="' + ((f.delay_min > 0 || f.delay_applied_min > 0) ? 'warn' : 'muted') + '">' +
          ((f.delay_min > 0 || f.delay_applied_min > 0)
            ? '+' + Math.max(f.delay_min, f.delay_applied_min) + '분' : '-') + '</td>' +
        '<td><input type="number" min="1" max="1440" value="30" id="delay-' + f.id + '">' +
          ' <button data-delay="' + f.id + '">지연 적용</button>' +
          ' <button class="ghost" data-detail="' + f.id + '">이력</button></td>';
      body.appendChild(tr);
    });
  }

  function renderEvents(data) {
    var box = $('events');
    box.innerHTML = '';
    (data.events || []).slice().reverse().forEach(function (e) {
      var el = document.createElement('div');
      el.className = 'event';
      el.innerHTML = '<b>' + e.status + '</b><span>' + fmt(e.ts) + ' · ' + e.note + '</span>';
      box.appendChild(el);
    });
  }

  function loadDetail(id) {
    selected = id;
    $('detailTitle').textContent = id + ' 운항 이력';
    fetch('/api/flights/' + id + '/timeline')
      .then(function (r) { return r.json(); })
      .then(renderEvents);
  }

  function refresh() {
    fetch('/api/ops/clock').then(function (r) { return r.json(); }).then(function (c) {
      serverNow = c.now;
      mode = c.mode;
      speed = c.speed;
      renderClock();
    });
    fetch('/api/flights').then(function (r) { return r.json(); }).then(renderRows);
  }

  document.addEventListener('click', function (e) {
    var d = e.target.getAttribute('data-delay');
    var t = e.target.getAttribute('data-detail');
    if (d) {
      var v = parseInt($('delay-' + d).value, 10);
      post('/api/ops/flights/' + d + '/delay', { delay_min: v })
        .then(function (r) {
          if (r.error) { toast(r.message || r.error, true); return; }
          toast(d + ' 편 지연 ' + v + '분 적용');
          refresh();
          loadDetail(d);
        });
      return;
    }
    if (t) loadDetail(t);
  });

  refresh();
  window.setInterval(refresh, 1000);
}());
