// Cloudflare Worker: serves the flat site from the ASSETS binding and exposes
// /api/check, which fetches a product page server-side (no CORS problems for
// the browser), analyses it, and folds the result into the page's price
// history in KV. Without a PRICE_HISTORY binding it still works, just with no
// memory between checks.

import { extractPage } from './extract.js';
import { addSnapshot, makeSnapshot, normaliseUrl, summarise } from './history.js';
import { assess, nextSteps } from './score.js';

const SECURITY_HEADERS = {
    'Content-Security-Policy': "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; connect-src 'self'; font-src 'self'; object-src 'none'; base-uri 'self'; form-action 'self'; frame-ancestors 'none'; upgrade-insecure-requests",
    'Strict-Transport-Security': 'max-age=63072000; includeSubDomains; preload',
    'X-Content-Type-Options': 'nosniff',
    'X-Frame-Options': 'DENY',
    'Referrer-Policy': 'strict-origin-when-cross-origin',
    'Permissions-Policy': 'camera=(), microphone=(), geolocation=(), payment=(), usb=()',
    'Cross-Origin-Opener-Policy': 'same-origin',
};

const FETCH_TIMEOUT_MS = 12000;
const MAX_BODY_BYTES = 2 * 1024 * 1024;
const RECENT_SNAPSHOTS = 90;

const withHeaders = (res, extra = {}) => {
    const out = new Response(res.body, res);
    for (const [k, v] of Object.entries({ ...SECURITY_HEADERS, ...extra })) out.headers.set(k, v);
    return out;
};

const json = (body, status = 200) => withHeaders(new Response(JSON.stringify(body), {
    status,
    headers: { 'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'no-store' },
}));

class CheckError extends Error {
    constructor(message, status = 400) { super(message); this.status = status; }
}

// Only public http(s) hosts. The Worker cannot resolve DNS itself, so this
// rejects the obvious literal forms; ALLOW_PRIVATE_TARGETS is for local dev.
export function validateTarget(input, env = {}) {
    let u;
    try { u = new URL(String(input).trim()); } catch { throw new CheckError('That does not look like a URL. Paste the full product page address, starting with https://'); }
    if (u.protocol !== 'http:' && u.protocol !== 'https:') throw new CheckError('Only http and https links can be checked.');
    if (u.username || u.password) throw new CheckError('Links with embedded credentials are not accepted.');
    const host = u.hostname.toLowerCase();
    const isPrivate = host === 'localhost' || host.endsWith('.localhost') || host.endsWith('.local') || host.endsWith('.internal')
        || /^(127\.|10\.|0\.|169\.254\.|192\.168\.|172\.(1[6-9]|2\d|3[01])\.)/.test(host)
        || host === '[::1]' || host.startsWith('[fc') || host.startsWith('[fd') || host.startsWith('[fe80');
    if (isPrivate && !env.ALLOW_PRIVATE_TARGETS) throw new CheckError('That address points at a private network.');
    return u;
}

async function fetchPage(target) {
    let res;
    try {
        res = await fetch(target.toString(), {
            redirect: 'follow',
            signal: AbortSignal.timeout(FETCH_TIMEOUT_MS),
            headers: {
                'User-Agent': 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/128.0 Safari/537.36 SaleCheck/1.0',
                Accept: 'text/html,application/xhtml+xml;q=0.9,*/*;q=0.8',
                'Accept-Language': 'en-GB,en;q=0.9',
            },
        });
    } catch (e) {
        throw new CheckError(`Could not fetch the page (${e.name === 'TimeoutError' ? 'timed out' : e.message}).`, 502);
    }
    if (!res.ok) throw new CheckError(`The shop answered with HTTP ${res.status}. Some sites block automated visits; save the page from your browser and use the CLI instead.`, 502);
    const type = res.headers.get('content-type') || '';
    if (type && !/html|xml|text\/plain/i.test(type)) throw new CheckError(`That link is not a web page (${type.split(';')[0]}).`);

    // Cap the body so a hostile or enormous page cannot eat the Worker's memory.
    const reader = res.body.getReader();
    const chunks = [];
    let total = 0;
    while (total < MAX_BODY_BYTES) {
        const { done, value } = await reader.read();
        if (done) break;
        chunks.push(value);
        total += value.byteLength;
    }
    reader.cancel().catch(() => {});
    const buf = new Uint8Array(total);
    let offset = 0;
    for (const c of chunks) { buf.set(c, offset); offset += c.byteLength; }
    return { html: new TextDecoder('utf-8').decode(buf.subarray(0, MAX_BODY_BYTES)), finalUrl: res.url || target.toString() };
}

async function historyKey(normalised) {
    const digest = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(normalised));
    return 'h:' + [...new Uint8Array(digest)].map((b) => b.toString(16).padStart(2, '0')).join('');
}

async function loadHistory(env, key) {
    if (!env.PRICE_HISTORY) return null;
    try { return (await env.PRICE_HISTORY.get(key, 'json')) || []; } catch { return []; }
}

async function readBuild(env, request) {
    try {
        const res = await env.ASSETS.fetch(new Request(new URL('/build.json', request.url)));
        if (res.ok) return await res.json();
    } catch { /* fall through */ }
    return { number: 0, commit: 'dev', date: null };
}

async function handleCheck(request, env, ctx) {
    let input;
    if (request.method === 'POST') {
        const body = await request.json().catch(() => ({}));
        input = body.url;
    } else {
        input = new URL(request.url).searchParams.get('url');
    }
    if (!input) throw new CheckError('Pass the product page as ?url=...');

    const target = validateTarget(input, env);
    const now = Date.now();
    const { html, finalUrl } = await fetchPage(target);
    const extract = extractPage(html, finalUrl, { now });

    const normalised = normaliseUrl(finalUrl);
    const key = await historyKey(normalised);
    const previous = await loadHistory(env, key);
    const history = addSnapshot(previous || [], makeSnapshot(extract, now));
    if (env.PRICE_HISTORY && extract.price != null) {
        ctx.waitUntil(env.PRICE_HISTORY.put(key, JSON.stringify(history)).catch(() => {}));
    }

    const summary = summarise(history, now);
    const assessment = assess(extract, summary, { now });

    return json({
        ok: true,
        checkedAt: new Date(now).toISOString(),
        url: target.toString(),
        finalUrl,
        product: extract,
        history: { ...summary, snapshots: history.slice(-RECENT_SNAPSHOTS) },
        assessment,
        nextSteps: nextSteps(finalUrl, extract.name),
        storage: env.PRICE_HISTORY ? 'kv' : 'none',
        build: await readBuild(env, request),
    });
}

export default {
    async fetch(request, env, ctx) {
        const url = new URL(request.url);
        try {
            if (url.pathname === '/api/check') {
                if (request.method !== 'GET' && request.method !== 'POST') return json({ ok: false, error: 'Method not allowed' }, 405);
                return await handleCheck(request, env, ctx);
            }
            if (url.pathname === '/api/version') {
                return json({ ok: true, build: await readBuild(env, request), env: env.APP_ENV || 'unknown', storage: env.PRICE_HISTORY ? 'kv' : 'none' });
            }
            if (url.pathname.startsWith('/api/')) return json({ ok: false, error: 'Not found' }, 404);
        } catch (e) {
            if (e instanceof CheckError) return json({ ok: false, error: e.message }, e.status);
            return json({ ok: false, error: 'Something went wrong analysing that page.' }, 500);
        }
        const asset = await env.ASSETS.fetch(request);
        return withHeaders(asset);
    },
};
