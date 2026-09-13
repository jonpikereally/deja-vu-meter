import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { extractPage, parseAmount, findMoney } from '../src/extract.js';

const fixture = (name) => readFileSync(new URL(`./fixtures/${name}`, import.meta.url), 'utf8');
const NOW = Date.parse('2026-09-13T12:00:00Z');

test('parseAmount handles UK, US and European formats', () => {
    assert.equal(parseAmount('1,299.99'), 1299.99);
    assert.equal(parseAmount('1.299,99'), 1299.99);
    assert.equal(parseAmount('1.299'), 1299);
    assert.equal(parseAmount('12,50'), 12.5);
    assert.equal(parseAmount('59.99'), 59.99);
    assert.equal(parseAmount('0'), null);
});

test('findMoney picks up symbol-first and symbol-last amounts', () => {
    const m = findMoney('Was £90.00 now £45.00 or 42,50 €');
    assert.deepEqual(m.map((x) => x.amount), [90, 45, 42.5]);
    assert.equal(m[0].currency, 'GBP');
    assert.equal(m[2].currency, 'EUR');
});

test('JSON-LD product with strikethrough price wins over text', () => {
    const p = extractPage(fixture('jsonld.html'), 'https://shop.example/widget', { now: NOW });
    assert.equal(p.name, 'Widget Pro 3000');
    assert.equal(p.price, 59.99);
    assert.equal(p.priceSource, 'json-ld');
    assert.equal(p.wasPrice, 99.99);
    assert.equal(p.wasSource, 'json-ld');
    assert.equal(p.currency, 'GBP');
    assert.equal(p.discountPct, 40);
    assert.equal(p.claimedPct, 40);
    assert.equal(p.priceValidUntil, '2030-01-01');
    assert.equal(p.availability, 'InStock');
    assert.equal(p.timerEnd, '2027-01-01T00:00:00.000Z');
    const kinds = p.signals.map((s) => s.kind);
    assert.ok(kinds.includes('countdown'), 'countdown');
    assert.ok(kinds.includes('scarcity'), 'scarcity');
    assert.ok(kinds.includes('social-proof'), 'social proof');
    assert.ok(!kinds.includes('evergreen-timer'), 'no inline timer script here');
    assert.equal(p.hasSaleClaim, true);
});

test('class-based old price and nearby current price without structured data', () => {
    const p = extractPage(fixture('strike.html'), 'https://lamps.example/bargain', { now: NOW });
    assert.equal(p.name, 'Bargain Lamp | Lamps R Us');
    assert.equal(p.wasPrice, 90);
    assert.equal(p.wasSource, 'strikethrough');
    assert.equal(p.price, 45);
    assert.equal(p.discountPct, 50);
    assert.equal(p.currency, 'GBP');
    assert.ok(p.signals.some((s) => s.kind === 'flash-sale'));
});

test('evergreen countdown script is detected', () => {
    const p = extractPage(fixture('evergreen.html'), 'https://blend.example/miracle', { now: NOW });
    assert.equal(p.wasPrice, 199);
    assert.equal(p.price, 49);
    assert.equal(p.currency, 'USD');
    const ever = p.signals.find((s) => s.kind === 'evergreen-timer');
    assert.ok(ever, 'evergreen timer flagged');
    assert.match(ever.code, /Date\.now\(\)\s*\+/);
    assert.ok(p.signals.some((s) => s.kind === 'countdown'));
    assert.ok(p.signals.some((s) => s.kind === 'up-to'));
});

test('a plain page with no discount has no sale claim', () => {
    const p = extractPage(fixture('nosale.html'), 'https://kettle.example/plain', { now: NOW });
    assert.equal(p.price, 34.99);
    assert.equal(p.priceSource, 'meta');
    assert.equal(p.wasPrice, null);
    assert.equal(p.hasSaleClaim, false);
    assert.equal(p.signals.length, 0);
});

test('european formatting with symbol after the amount', () => {
    const p = extractPage(fixture('euro.html'), 'https://kaffee.example/maschine', { now: NOW });
    assert.equal(p.wasPrice, 1299);
    assert.equal(p.price, 999);
    assert.equal(p.currency, 'EUR');
});

test('"% off" without a shown reference price derives one', () => {
    const html = '<html><body><h1>Thing</h1><p>Price £70.00</p><p>30% off this week</p></body></html>';
    const p = extractPage(html, 'https://x.example/t', { now: NOW });
    assert.equal(p.price, 70);
    assert.equal(p.wasSource, 'derived');
    assert.equal(p.wasPrice, 100);
});

test('a "was" price that is not higher than the price is discarded', () => {
    const html = '<html><body><p>Was £20.00</p><p>Now £25.00</p></body></html>';
    const p = extractPage(html, 'https://x.example/t', { now: NOW });
    assert.equal(p.wasPrice, null);
    assert.equal(p.hasSaleClaim, false);
});
