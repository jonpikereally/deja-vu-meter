import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { extractPage } from '../src/extract.js';
import { summarise } from '../src/history.js';
import { assess, nextSteps } from '../src/score.js';

const DAY = 86400000;
const T0 = Date.parse('2026-08-01T10:00:00Z');
const NOW = T0 + 40 * DAY;
const fixture = (name) => readFileSync(new URL(`./fixtures/${name}`, import.meta.url), 'utf8');
const snap = (day, price, wasPrice = null, extra = {}) => ({ ts: T0 + day * DAY, price, wasPrice, ...extra });
const clean = { name: 'Thing', currency: 'GBP', price: 60, wasPrice: 100, wasSource: 'strikethrough', discountPct: 40, claimedPct: null, upTo: false, signals: [] };

test('first check of a clean sale page is unverified, not genuine', () => {
    const a = assess(clean, summarise([snap(40, 60, 100)], NOW), { now: NOW });
    assert.equal(a.verdict, 'unverified');
    assert.equal(a.confidence, 'none');
    assert.ok(a.findings.some((f) => f.title === 'No price history yet'));
});

test('a month at the reference price before the sale is genuine', () => {
    const h = [];
    for (let d = 0; d < 35; d += 5) h.push(snap(d, 100));
    h.push(snap(35, 60, 100));
    const a = assess(clean, summarise(h, T0 + 36 * DAY), { now: T0 + 36 * DAY });
    assert.equal(a.verdict, 'genuine');
    assert.equal(a.score, 0);
    assert.ok(a.findings.some((f) => f.severity === 'green' && f.title === 'Reference price verified'));
});

test('a perpetual sale with a never-charged was-price is fake', () => {
    const h = [];
    for (let d = 0; d <= 40; d += 4) h.push(snap(d, 60, 100));
    const a = assess(clean, summarise(h, NOW + DAY), { now: NOW + DAY });
    assert.equal(a.verdict, 'fake');
    assert.equal(a.confidence, 'high');
    const titles = a.findings.map((f) => f.title);
    assert.ok(titles.includes('Perpetual sale'));
    assert.ok(titles.includes('Reference price never charged'));
});

test('evergreen countdown page is suspicious on the very first visit', () => {
    const p = extractPage(fixture('evergreen.html'), 'https://blend.example/miracle', { now: NOW });
    const a = assess(p, summarise([snap(40, p.price, p.wasPrice)], NOW), { now: NOW });
    assert.ok(a.score >= 40, `score ${a.score}`);
    assert.ok(['suspicious', 'fake'].includes(a.verdict), a.verdict);
    assert.equal(a.findings[0].severity, 'red');
    assert.equal(a.findings[0].title, 'Fake countdown timer');
});

test('a page with no discount reports no sale', () => {
    const p = extractPage(fixture('nosale.html'), 'https://kettle.example/plain', { now: NOW });
    const a = assess(p, summarise([snap(40, p.price)], NOW), { now: NOW });
    assert.equal(a.verdict, 'no-sale');
});

test('RRP-based discount and pressure tactics add up to a warning', () => {
    const p = extractPage(fixture('jsonld.html'), 'https://shop.example/widget', { now: NOW });
    const a = assess(p, summarise([snap(40, p.price, p.wasPrice)], NOW), { now: NOW });
    assert.equal(a.verdict, 'warning');
    assert.ok(a.findings.some((f) => f.title === 'Scarcity claim'));
    assert.ok(a.findings.some((f) => f.title === 'Live-activity claim'));
});

test('a timer that restarts between visits is a red finding', () => {
    const h = [snap(38, 60, 100, { timerEnd: '2026-09-09T00:00:00Z' }), snap(40, 60, 100, { timerEnd: '2026-09-11T00:00:00Z' })];
    const a = assess(clean, summarise(h, NOW), { now: NOW });
    assert.ok(a.findings.some((f) => f.title === 'Countdown restarted' && f.severity === 'red'));
});

test('nextSteps links camelcamelcamel for Amazon and Wayback for everything', () => {
    const amazon = nextSteps('https://www.amazon.co.uk/dp/B0ABCDEFGH?ref=x', 'Thing');
    assert.equal(amazon[0].href, 'https://camelcamelcamel.com/product/B0ABCDEFGH');
    const other = nextSteps('https://shop.example/p', 'Thing');
    assert.ok(other.every((s) => !/camel/.test(s.href)));
    assert.ok(other.some((s) => /web\.archive\.org/.test(s.href)));
});
