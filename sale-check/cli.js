#!/usr/bin/env node
// Local front door to the same analysis the Worker runs, with price history
// kept in a JSON file. Useful when a shop blocks automated visits: save the
// page from your browser and pass it with --html.
//
//   node cli.js https://shop.example/product
//   node cli.js https://shop.example/product --html saved-page.html
//   node cli.js https://shop.example/product --json

import { readFileSync, writeFileSync, existsSync } from 'node:fs';
import { homedir } from 'node:os';
import { join } from 'node:path';
import { extractPage } from './src/extract.js';
import { addSnapshot, makeSnapshot, normaliseUrl, summarise } from './src/history.js';
import { assess, formatMoney, nextSteps } from './src/score.js';

const args = process.argv.slice(2);
const flag = (name) => { const i = args.indexOf(name); return i >= 0 ? args[i + 1] : null; };
const has = (name) => args.includes(name);
const url = args.find((a) => /^https?:\/\//i.test(a));

if (!url || has('--help')) {
    console.log('Usage: node cli.js <product-url> [--html saved-page.html] [--history file.json] [--json]');
    process.exit(url ? 0 : 1);
}

const historyPath = flag('--history') || join(homedir(), '.sale-check-history.json');
const htmlPath = flag('--html');

async function loadHtml() {
    if (htmlPath) return readFileSync(htmlPath, 'utf8');
    const res = await fetch(url, {
        redirect: 'follow',
        signal: AbortSignal.timeout(15000),
        headers: {
            'User-Agent': 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/128.0 Safari/537.36',
            Accept: 'text/html,application/xhtml+xml;q=0.9,*/*;q=0.8',
            'Accept-Language': 'en-GB,en;q=0.9',
        },
    });
    if (!res.ok) throw new Error(`HTTP ${res.status} from the shop. Save the page in your browser and re-run with --html <file>.`);
    return res.text();
}

const now = Date.now();
const html = await loadHtml();
const extract = extractPage(html, url, { now });

const store = existsSync(historyPath) ? JSON.parse(readFileSync(historyPath, 'utf8')) : {};
const key = normaliseUrl(url);
const history = addSnapshot(store[key] || [], makeSnapshot(extract, now));
if (extract.price != null) {
    store[key] = history;
    writeFileSync(historyPath, JSON.stringify(store, null, 2) + '\n');
}
const summary = summarise(history, now);
const assessment = assess(extract, summary, { now });
const steps = nextSteps(url, extract.name);

if (has('--json')) {
    console.log(JSON.stringify({ url, product: extract, history: summary, assessment, nextSteps: steps }, null, 2));
    process.exit(0);
}

const tty = process.stdout.isTTY;
const paint = (code, s) => (tty ? `\x1b[${code}m${s}\x1b[0m` : s);
const colour = { red: 31, amber: 33, green: 32, info: 36 };
const verdictColour = { fake: 31, suspicious: 31, warning: 33, unverified: 36, 'probably-genuine': 32, genuine: 32, 'no-sale': 36 };
const money = (v) => formatMoney(v, extract.currency);

console.log();
console.log(paint(1, extract.name || url));
console.log(`  Price now: ${money(extract.price)}` + (extract.wasPrice != null ? `   Claimed was: ${money(extract.wasPrice)} (${extract.discountPct}% off, ${extract.wasSource})` : ''));
console.log();
console.log(`${paint(verdictColour[assessment.verdict], paint(1, assessment.label.toUpperCase()))}  suspicion ${assessment.score}/100, confidence ${assessment.confidence}`);
console.log(`  ${assessment.summary}`);
console.log();
for (const f of assessment.findings) {
    console.log(`  ${paint(colour[f.severity], f.severity.toUpperCase().padEnd(5))} ${paint(1, f.title)}`);
    console.log(`        ${f.detail}`);
}
console.log();
console.log(`  History: ${summary.observations} check${summary.observations === 1 ? '' : 's'} over ${summary.spanDays.toFixed(1)} days`
    + (summary.observations ? `, lowest seen ${money(summary.minPrice)}` : '') + ` (stored in ${historyPath})`);
console.log();
console.log('  Check for yourself:');
for (const s of steps) console.log(`    - ${s.label}: ${s.href}`);
console.log();
