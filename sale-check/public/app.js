(function () {
  'use strict';
  var $ = function (id) { return document.getElementById(id); };
  var form = $('check-form');
  var status = $('status');
  var result = $('result');
  var submit = $('submit');
  var SYMBOLS = { GBP: '£', EUR: '€', USD: '$', AUD: 'A$', CAD: 'CA$', NZD: 'NZ$' };
  var BADGE = { fake: 'red', suspicious: 'red', warning: 'amber', unverified: '', 'probably-genuine': 'green', genuine: 'green', 'no-sale': '' };
  var SOURCE = {
    'json-ld': 'crossed-out price in the page\'s structured data',
    meta: 'original price in the page\'s metadata',
    strikethrough: 'crossed-out price on the page',
    rrp: 'RRP quoted on the page',
    'was-label': '"was" price quoted on the page',
    derived: 'implied by the "% off" claim (no reference price shown)'
  };

  function money(v, cur) {
    if (v == null) return '—';
    var s = SYMBOLS[cur];
    var n = Number(v).toFixed(2);
    return s ? s + n : cur ? n + ' ' + cur : n;
  }
  function setStatus(text, isError) {
    status.textContent = text;
    status.hidden = !text;
    status.classList.toggle('error', !!isError);
  }
  function clear(el) { while (el.firstChild) el.removeChild(el.firstChild); }
  function el(tag, cls, text) {
    var e = document.createElement(tag);
    if (cls) e.className = cls;
    if (text != null) e.textContent = text;
    return e;
  }

  function drawSpark(snaps, cur) {
    var svg = $('spark');
    clear(svg);
    var pts = (snaps || []).filter(function (s) { return s.price != null; });
    if (pts.length < 2) {
      var t = document.createElementNS(svg.namespaceURI, 'text');
      t.setAttribute('x', '160'); t.setAttribute('y', '50'); t.setAttribute('text-anchor', 'middle');
      t.setAttribute('fill', 'currentColor'); t.setAttribute('font-size', '12');
      t.textContent = pts.length ? 'First check recorded. Come back later to see a trend.' : 'No price recorded.';
      svg.appendChild(t);
      return;
    }
    var vals = [];
    pts.forEach(function (s) { vals.push(s.price); if (s.wasPrice != null) vals.push(s.wasPrice); });
    var min = Math.min.apply(null, vals), max = Math.max.apply(null, vals);
    if (max === min) { max = min * 1.1; min = min * 0.9; }
    var t0 = pts[0].ts, t1 = pts[pts.length - 1].ts, span = Math.max(1, t1 - t0);
    var X = function (ts) { return 8 + 304 * (ts - t0) / span; };
    var Y = function (v) { return 82 - 74 * (v - min) / (max - min); };
    var path = function (key, cls) {
      var d = '', started = false;
      pts.forEach(function (s) {
        if (s[key] == null) { started = false; return; }
        d += (started ? ' L' : ' M') + X(s.ts).toFixed(1) + ' ' + Y(s[key]).toFixed(1);
        started = true;
      });
      var p = document.createElementNS(svg.namespaceURI, 'path');
      p.setAttribute('d', d.trim()); p.setAttribute('class', cls);
      svg.appendChild(p);
    };
    path('wasPrice', 'was');
    path('price', 'line');
    var last = pts[pts.length - 1];
    var c = document.createElementNS(svg.namespaceURI, 'circle');
    c.setAttribute('cx', X(last.ts).toFixed(1)); c.setAttribute('cy', Y(last.price).toFixed(1)); c.setAttribute('r', '3'); c.setAttribute('class', 'dot');
    svg.appendChild(c);
    var title = document.createElementNS(svg.namespaceURI, 'title');
    title.textContent = 'Solid: price paid. Dashed: claimed "was" price. Now ' + money(last.price, cur);
    svg.appendChild(title);
  }

  function render(data) {
    var p = data.product, a = data.assessment, h = data.history || {};
    var badge = $('verdict-badge');
    badge.textContent = a.label;
    badge.className = 'badge ' + (BADGE[a.verdict] || '');
    $('confidence').textContent = 'History confidence: ' + a.confidence + (data.storage === 'none' ? ' (no storage configured, so nothing is remembered between checks)' : '');
    $('verdict-summary').textContent = a.summary;
    $('meter-fill').style.width = (100 - a.score) + '%';
    $('score-text').textContent = 'Suspicion ' + a.score + '/100';

    $('p-name').textContent = p.name || '(no name found)';
    $('p-price').textContent = money(p.price, p.currency);
    $('p-was').textContent = p.wasPrice != null ? money(p.wasPrice, p.currency) : 'none claimed';
    $('p-discount').textContent = p.discountPct != null ? p.discountPct + '% off' : (p.claimedPct != null ? (p.upTo ? 'up to ' : '') + p.claimedPct + '% off (claimed)' : '—');
    $('p-source').textContent = p.wasSource ? (SOURCE[p.wasSource] || p.wasSource) : '—';

    drawSpark(h.snapshots, p.currency);
    var obs = h.observations || 0;
    $('history-text').textContent = obs
      ? obs + ' check' + (obs === 1 ? '' : 's') + ' over ' + (h.spanDays < 1 ? 'less than a day' : Math.round(h.spanDays) + ' days') +
        (obs > 1 ? '. Lowest seen ' + money(h.minPrice, p.currency) + '.' : '. Check again in a few days.')
      : 'No history for this page.';

    var list = $('findings');
    clear(list);
    a.findings.forEach(function (f) {
      var li = el('li', f.severity);
      li.appendChild(el('span', 'title', f.title));
      li.appendChild(el('span', 'detail', f.detail));
      list.appendChild(li);
    });

    var steps = $('next-steps');
    clear(steps);
    (data.nextSteps || []).forEach(function (s) {
      var li = el('li');
      var link = el('a', null, s.label);
      link.href = s.href; link.target = '_blank'; link.rel = 'noopener noreferrer';
      li.appendChild(link);
      steps.appendChild(li);
    });

    result.hidden = false;
    result.scrollIntoView({ behavior: 'smooth', block: 'start' });
  }

  form.addEventListener('submit', function (ev) {
    ev.preventDefault();
    var url = $('url').value.trim();
    if (!url) return;
    submit.disabled = true;
    result.hidden = true;
    setStatus('Fetching the page and reading its prices…');
    fetch('/api/check?url=' + encodeURIComponent(url))
      .then(function (r) { return r.json().then(function (j) { return { ok: r.ok, body: j }; }); })
      .then(function (r) {
        if (!r.ok || !r.body.ok) throw new Error(r.body.error || 'Unexpected error');
        setStatus('');
        render(r.body);
        try { history.replaceState(null, '', '?url=' + encodeURIComponent(url)); } catch (e) { /* ignore */ }
      })
      .catch(function (e) { setStatus(e.message, true); })
      .then(function () { submit.disabled = false; });
  });

  fetch('/build.json').then(function (r) { return r.ok ? r.json() : null; }).then(function (b) {
    if (!b) return;
    $('build').textContent = 'build ' + b.number + ' (' + b.commit + ')' + (b.date ? ', ' + new Date(b.date).toLocaleString() : '');
  }).catch(function () {});

  var preset = new URLSearchParams(location.search).get('url');
  if (preset) { $('url').value = preset; form.requestSubmit ? form.requestSubmit() : submit.click(); }
})();
