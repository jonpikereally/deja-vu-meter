import { test } from 'node:test';
import assert from 'node:assert/strict';
import { addSnapshot, normaliseUrl, summarise } from '../src/history.js';

const DAY = 86400000;
const T0 = Date.parse('2026-08-01T10:00:00Z');
const snap = (day, price, wasPrice = null, extra = {}) => ({ ts: T0 + day * DAY, price, wasPrice, ...extra });

test('normaliseUrl strips tracking params, fragments, www and trailing slash', () => {
    assert.equal(normaliseUrl('https://www.Shop.example/p/1/?utm_source=x&b=2&a=1#top'), 'https://shop.example/p/1?a=1&b=2');
    assert.equal(normaliseUrl('https://shop.example/'), 'https://shop.example/');
});

test('addSnapshot drops repeats within six hours and caps the list', () => {
    let h = addSnapshot([], snap(0, 10, 20));
    h = addSnapshot(h, { ...snap(0, 10, 20), ts: T0 + 3600000 });
    assert.equal(h.length, 1);
    h = addSnapshot(h, { ...snap(0, 9, 20), ts: T0 + 3600000 });
    assert.equal(h.length, 2, 'a price change is always recorded');
    for (let i = 0; i < 500; i++) h = addSnapshot(h, snap(i + 1, i, null));
    assert.equal(h.length, 400);
});

test('reference price charged for a month before the sale is verified', () => {
    const h = [];
    for (let d = 0; d < 35; d += 5) h.push(snap(d, 100));
    h.push(snap(35, 60, 100));
    const s = summarise(h, T0 + 36 * DAY);
    assert.equal(s.latestOnSale, true);
    assert.equal(s.referenceEverCharged, true);
    assert.ok(s.daysAtReferenceBeforeSale >= 30, `days at ref ${s.daysAtReferenceBeforeSale}`);
    assert.equal(s.currentRunDays > 0 && s.currentRunDays < 2, true);
});

test('perpetual sale: every observation on sale over a month', () => {
    const h = [];
    for (let d = 0; d <= 40; d += 4) h.push(snap(d, 60, 100));
    const s = summarise(h, T0 + 41 * DAY);
    assert.equal(s.saleFraction, 1);
    assert.equal(s.referenceEverCharged, false);
    assert.ok(s.currentRunDays >= 40);
});

test('rising was-price, yo-yo cycles and timer restarts are counted', () => {
    const h = [
        snap(0, 60, 90, { timerEnd: '2026-08-03T00:00:00Z' }),
        snap(2, 60, 90, { timerEnd: '2026-08-05T00:00:00Z' }),
        snap(4, 90),
        snap(6, 60, 100),
        snap(8, 90),
        snap(10, 60, 110),
    ];
    const s = summarise(h, T0 + 11 * DAY);
    assert.equal(s.referencePriceRose, true);
    assert.equal(s.saleCycles, 2);
    assert.equal(s.timerRestarts, 1);
    assert.equal(s.minPrice, 60);
    assert.equal(s.maxPrice, 90);
});

test('empty history summarises to zero observations', () => {
    assert.deepEqual(summarise([], T0), { observations: 0, spanDays: 0 });
});
