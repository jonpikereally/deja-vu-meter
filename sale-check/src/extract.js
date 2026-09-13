// Pulls the facts a shopper cares about out of a product page: what it costs
// now, what the shop claims it used to cost, and which pressure tactics the
// page is leaning on. Everything here is a pure function of the HTML so it can
// be unit-tested offline against saved pages, and so the same code runs in the
// Worker and in the CLI.

const SYMBOL_TO_CODE = { '£': 'GBP', '€': 'EUR', '$': 'USD', 'US$': 'USD', 'CA$': 'CAD', 'A$': 'AUD' };
const CODE_IN_TEXT = /\b(GBP|EUR|USD|AUD|CAD|NZD|CHF|SEK|NOK|DKK|PLN)\b/;

// Symbol-first ("£1,299.99") or symbol-last ("1.299,99 €") amounts. Built fresh
// per call because a shared global regex carries lastIndex between calls.
const moneyRe = () =>
    /(US\$|CA\$|A\$|£|€|\$)\s?(\d{1,3}(?:[.,]\d{3})+(?:[.,]\d{1,2})?|\d+(?:[.,]\d{1,2})?)|(\d{1,3}(?:[.,]\d{3})+(?:[.,]\d{1,2})?|\d+(?:[.,]\d{1,2})?)\s?(£|€|\$)/g;

// Visible-text patterns for the usual pressure tactics. Each yields one signal
// with the matched snippet as evidence.
const SIGNAL_PATTERNS = [
    ['countdown', /\b(?:sale|offer|deal|price|discount)s?\s+ends?\s+(?:in|soon|today|tonight|at midnight)\b|\bends?\s+in\s+\d|\b\d{1,2}\s*:\s*\d{2}\s*:\s*\d{2}\b|\bhurry\b|\blast chance\b|\b\d+\s*(?:hrs?|hours?|mins?|minutes?|days?)\s+(?:left|remaining)\b|\bcountdown\b/i],
    ['flash-sale', /\bflash sale\b|\btoday only\b|\blimited[- ]time\b|\bwhile stocks? lasts?\b|\bdon'?t miss out\b|\bonce it'?s gone\b/i],
    ['scarcity', /\bonly\s+\d+\s+left\b|\b\d+\s+left in stock\b|\blow stock\b|\balmost gone\b|\bselling fast\b|\bnearly sold out\b|\bin high demand\b|\bgoing fast\b/i],
    ['social-proof', /\b\d+\s+(?:people|others|customers|shoppers)\s+(?:are\s+)?(?:viewing|looking|watching|bought|have bought|purchased)\b|\bbought in the last\b|\b\d+\s+sold\s+(?:today|recently|in the last)\b|\btrending now\b/i],
    ['up-to', /\bup to\s+\d{1,3}\s?%\s?off\b/i],
];

// Inline-script patterns that give away a countdown with no real deadline.
// Only applied to script blocks that also talk about a timer, because
// "Date.now() + n" on its own is everywhere (cache-busting, analytics).
const TIMER_WORDS = /countdown|timer|deadline|expir|ends?[_-]?(?:in|at)|remaining|hurry|time[_-]?left/i;
const EVERGREEN_PATTERNS = [
    [/(?:Date\.now\(\)|new\s+Date\(\)(?:\.getTime\(\))?|\.getTime\(\))\s*\+\s*(?:\d|\(|[A-Za-z_$][\w$.]*\s*\*)/,
        'The countdown deadline is computed from the visitor\'s clock, so it restarts on every visit.'],
    [/setHours\(\s*(?:23|24)\s*,\s*59/,
        'The countdown resets at midnight every day rather than ending.'],
    [/localStorage|sessionStorage|document\.cookie/,
        'The countdown deadline is stored per visitor rather than set by the shop.'],
    [/\b(?:evergreen|hurrify|fomo)\b/i,
        'Uses a known "evergreen" urgency-timer technique.'],
];

const TIMER_ATTR = /\bdata-(?:countdown(?:-?(?:end|to|date|target))?|end(?:-?(?:time|date|at))?|deadline|expires?|expiry|until|target(?:-?date)?)\s*=\s*["']([^"']+)["']/gi;

const round2 = (n) => Math.round(n * 100) / 100;
const toArray = (v) => (v == null ? [] : Array.isArray(v) ? v : [v]);

export function parseAmount(raw) {
    if (raw == null) return null;
    let s = String(raw).replace(/\s/g, '');
    const lastComma = s.lastIndexOf(',');
    const lastDot = s.lastIndexOf('.');
    if (lastComma >= 0 && lastDot >= 0) {
        // Whichever separator comes last is the decimal point.
        const decimal = lastComma > lastDot ? ',' : '.';
        const thousands = decimal === ',' ? '.' : ',';
        s = s.split(thousands).join('').replace(decimal, '.');
    } else if (lastComma >= 0) {
        const after = s.length - lastComma - 1;
        s = after === 3 ? s.replace(/,/g, '') : s.replace(',', '.');
    } else if (lastDot >= 0) {
        const dots = s.split('.').length - 1;
        const after = s.length - lastDot - 1;
        // "1.299" is almost always European thousands; money rarely has 3 decimals.
        if (dots > 1 || after === 3) s = s.replace(/\./g, '');
    }
    const n = Number(s);
    return Number.isFinite(n) && n > 0 ? round2(n) : null;
}

export function decodeEntities(s) {
    return s
        .replace(/&nbsp;/gi, ' ')
        .replace(/&pound;/gi, '£')
        .replace(/&euro;/gi, '€')
        .replace(/&amp;/gi, '&')
        .replace(/&lt;/gi, '<')
        .replace(/&gt;/gi, '>')
        .replace(/&quot;/gi, '"')
        .replace(/&#(\d+);/g, (_, n) => String.fromCodePoint(Number(n)))
        .replace(/&#x([0-9a-f]+);/gi, (_, h) => String.fromCodePoint(parseInt(h, 16)));
}

// Roughly what a person sees: scripts, styles and tags removed, block
// boundaries kept as newlines so labels don't run into unrelated prices.
export function visibleText(html) {
    return decodeEntities(
        html
            .replace(/<!--[\s\S]*?-->/g, ' ')
            .replace(/<(script|style|noscript|template|svg)\b[^>]*>[\s\S]*?<\/\1>/gi, ' ')
            .replace(/<br\s*\/?>|<\/(?:p|div|li|tr|h[1-6]|section|article|td|th|dd|dt|span)>/gi, '\n')
            .replace(/<[^>]+>/g, ' '))
        .replace(/[ \t\r\f\v]+/g, ' ')
        .replace(/\s*\n\s*/g, '\n')
        .trim();
}

export function findMoney(text) {
    const out = [];
    const re = moneyRe();
    let m;
    while ((m = re.exec(text))) {
        const symbol = m[1] || m[4];
        const amount = parseAmount(m[2] || m[3]);
        if (amount == null) continue;
        out.push({ amount, currency: SYMBOL_TO_CODE[symbol] || null, index: m.index, raw: m[0] });
    }
    return out;
}

function parseAttrs(tag) {
    const attrs = {};
    const re = /([a-zA-Z_:][-\w:.]*)\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s"'>]+))/g;
    let m;
    while ((m = re.exec(tag))) attrs[m[1].toLowerCase()] = decodeEntities(m[2] ?? m[3] ?? m[4] ?? '');
    return attrs;
}

function parseJsonLoose(src) {
    const cleaned = src.replace(/^\s*<!\[CDATA\[|\]\]>\s*$/g, '').trim();
    try { return JSON.parse(cleaned); } catch { /* fall through */ }
    try { return JSON.parse(cleaned.replace(/,\s*([}\]])/g, '$1')); } catch { return null; }
}

function hasType(node, type) {
    const t = node && node['@type'];
    return Array.isArray(t) ? t.includes(type) : t === type;
}

function walk(node, visit) {
    if (Array.isArray(node)) { node.forEach((n) => walk(n, visit)); return; }
    if (!node || typeof node !== 'object') return;
    visit(node);
    Object.values(node).forEach((v) => walk(v, visit));
}

function toNumber(v) {
    if (typeof v === 'number') return v > 0 ? round2(v) : null;
    if (typeof v === 'string') return parseAmount(v.replace(/[^\d.,]/g, ''));
    return null;
}

function readOffer(o) {
    const r = { price: null, wasPrice: null, currency: null, priceValidUntil: null, availability: null };
    r.price = toNumber(o.price ?? o.lowPrice);
    r.currency = typeof o.priceCurrency === 'string' ? o.priceCurrency.toUpperCase() : null;
    r.priceValidUntil = typeof o.priceValidUntil === 'string' ? o.priceValidUntil : null;
    r.availability = typeof o.availability === 'string' ? o.availability.replace(/^https?:\/\/schema\.org\//, '') : null;
    let salePrice = null;
    for (const ps of toArray(o.priceSpecification)) {
        if (!ps || typeof ps !== 'object') continue;
        const type = String(ps.priceType || '');
        const p = toNumber(ps.price);
        if (p == null) continue;
        if (/StrikethroughPrice|ListPrice|MSRP|RRP|RegularPrice/i.test(type)) r.wasPrice ??= p;
        else if (/SalePrice/i.test(type)) salePrice ??= p;
        else if (!type) salePrice ??= p;
        r.currency ??= typeof ps.priceCurrency === 'string' ? ps.priceCurrency.toUpperCase() : null;
    }
    if (r.price == null) r.price = salePrice;
    return r;
}

export function extractJsonLd(html) {
    const products = [];
    const looseOffers = [];
    const re = /<script\b[^>]*type\s*=\s*["']application\/ld\+json["'][^>]*>([\s\S]*?)<\/script>/gi;
    let m;
    while ((m = re.exec(html))) {
        const data = parseJsonLoose(m[1]);
        if (!data) continue;
        walk(data, (node) => {
            if (hasType(node, 'Product') || hasType(node, 'ProductGroup') || hasType(node, 'IndividualProduct')) products.push(node);
            else if (hasType(node, 'Offer') || hasType(node, 'AggregateOffer')) looseOffers.push(node);
        });
    }
    const out = { found: products.length > 0 || looseOffers.length > 0, name: null, price: null, wasPrice: null, currency: null, priceValidUntil: null, availability: null };
    const product = products.find((p) => p.offers) || products[0];
    if (product && typeof product.name === 'string') out.name = product.name.trim();
    const offers = product ? toArray(product.offers) : looseOffers;
    for (const o of offers) {
        if (!o || typeof o !== 'object') continue;
        const r = readOffer(o);
        for (const k of Object.keys(r)) out[k] ??= r[k];
    }
    return out;
}

export function extractMeta(html) {
    const map = {};
    const tagRe = /<meta\b[^>]*>/gi;
    let t;
    while ((t = tagRe.exec(html))) {
        const a = parseAttrs(t[0]);
        const key = (a.property || a.name || a.itemprop || '').toLowerCase();
        if (key && a.content != null && !(key in map)) map[key] = a.content;
    }
    const ipRe = /<[a-z]+\b[^>]*\bitemprop\s*=\s*["']price["'][^>]*>/gi;
    while ((t = ipRe.exec(html))) {
        const a = parseAttrs(t[0]);
        if (a.content != null && !('price' in map)) map.price = a.content;
    }
    const num = (k) => (map[k] != null ? toNumber(map[k]) : null);
    const sale = num('product:sale_price:amount');
    const list = num('product:price:amount') ?? num('og:price:amount') ?? num('price');
    return {
        raw: map,
        price: sale ?? list,
        originalPrice: num('product:original_price:amount') ?? (sale != null && list != null && list > sale ? list : null),
        currency: (map['product:price:currency'] || map['og:price:currency'] || map.pricecurrency || null)?.toUpperCase() || null,
        title: map['og:title'] || null,
    };
}

// A crossed-out price is the clearest "was" claim a page can make.
export function extractStrikethrough(html) {
    const tagRe = /<(s|del|strike)\b[^>]*>([\s\S]*?)<\/\1>/gi;
    let m;
    while ((m = tagRe.exec(html))) {
        const money = findMoney(visibleText(m[2]));
        if (money.length) return { amount: money[0].amount, currency: money[0].currency, how: 'strikethrough' };
    }
    const classRe = /<([a-z]+)\b[^>]*class\s*=\s*["'][^"']*(?:(?:was|rrp|old|original|compare|regular|previous|strike|crossed)[-_]?price|price[-_]{1,2}(?:was|old|original|regular|previous|strike|compare|rrp)|\b(?:was|rrp|strikethrough|strike-through|line-through)\b)[^"']*["'][^>]*>([\s\S]{0,300}?)<\/\1>/gi;
    while ((m = classRe.exec(html))) {
        const money = findMoney(visibleText(m[2]));
        if (money.length) return { amount: money[0].amount, currency: money[0].currency, how: 'class' };
    }
    return null;
}

function labelledPrices(text, moneys) {
    const out = { now: null, was: null, rrp: null, save: null, wasIndex: null };
    const grab = (re, key) => {
        let m;
        while ((m = re.exec(text))) {
            const end = m.index + m[0].length;
            const money = moneys.find((x) => x.index >= end && x.index - end <= 12);
            if (money) {
                out[key] = money.amount;
                if (key === 'was' || key === 'rrp') out.wasIndex ??= money.index;
                return;
            }
        }
    };
    grab(/\b(?:rrp|r\.r\.p\.?|recommended retail price|list price|msrp)\b[:\s]*/gi, 'rrp');
    grab(/\b(?:was|regular price|original price|previously|previous price|compare at|before)\b[:\s]*/gi, 'was');
    grab(/\b(?:now|sale price|our price|today|now only|(?<!(?:list|regular|original|old|previous|compare)\s)price)\b[:\s]*/gi, 'now');
    grab(/\bsave\b[:\s]*/gi, 'save');
    return out;
}

function discountClaim(text) {
    const m = /\b(?:save|get)\s+(?:up to\s+)?(\d{1,2})\s?%/i.exec(text) || /(\d{1,2})\s?%\s?off\b/i.exec(text);
    if (!m) return { pct: null, upTo: false, evidence: null };
    const upTo = /\bup to\s+\d{1,3}\s?%/i.test(m[0]) || /\bup to\s*$/i.test(text.slice(Math.max(0, m.index - 8), m.index));
    return { pct: Number(m[1]), upTo, evidence: snippet(text, m.index, m[0].length) };
}

// A little context either side of a match, trimmed to word boundaries so the
// finding reads like a quote rather than a fragment.
function snippet(text, index, length) {
    let start = Math.max(0, index - 30);
    let end = Math.min(text.length, index + length + 30);
    while (start > 0 && /\S/.test(text[start - 1])) start--;
    while (end < text.length && /\S/.test(text[end])) end++;
    const s = text.slice(start, end).replace(/\s+/g, ' ').trim();
    return (start > 0 ? '...' : '') + s + (end < text.length ? '...' : '');
}

function inlineScripts(html) {
    const out = [];
    const re = /<script\b([^>]*)>([\s\S]*?)<\/script>/gi;
    let m;
    while ((m = re.exec(html))) {
        if (/\bsrc\s*=/i.test(m[1]) || /ld\+json/i.test(m[1])) continue;
        if (m[2].trim()) out.push(m[2]);
    }
    return out;
}

export function detectSignals(html, text) {
    const signals = [];
    for (const [kind, re] of SIGNAL_PATTERNS) {
        const m = re.exec(text);
        if (m) signals.push({ kind, evidence: snippet(text, m.index, m[0].length) });
    }
    for (const script of inlineScripts(html)) {
        if (!TIMER_WORDS.test(script)) continue;
        for (const [re, why] of EVERGREEN_PATTERNS) {
            const m = re.exec(script);
            if (m) {
                signals.push({ kind: 'evergreen-timer', evidence: why, code: m[0].slice(0, 80) });
                return signals;
            }
        }
    }
    return signals;
}

// Best-effort read of a countdown's advertised deadline so later checks can
// tell whether it genuinely ended or just quietly restarted.
export function extractTimerEnd(html, now = Date.now()) {
    const re = new RegExp(TIMER_ATTR.source, 'gi');
    let m;
    while ((m = re.exec(html))) {
        const v = m[1].trim();
        let ts = null;
        if (/^\d{9,13}$/.test(v)) ts = v.length <= 10 ? Number(v) * 1000 : Number(v);
        else ts = Date.parse(v);
        if (!Number.isFinite(ts)) continue;
        if (ts < now - 30 * 86400000 || ts > now + 730 * 86400000) continue;
        return new Date(ts).toISOString();
    }
    return null;
}

function titleOf(html) {
    const m = /<title[^>]*>([\s\S]*?)<\/title>/i.exec(html);
    return m ? visibleText(m[1]) : null;
}

export function extractPage(html, url = '', opts = {}) {
    const now = opts.now ?? Date.now();
    const text = visibleText(html);
    const ld = extractJsonLd(html);
    const meta = extractMeta(html);
    const strike = extractStrikethrough(html);
    const moneys = findMoney(text);
    const labelled = labelledPrices(text, moneys);
    const claim = discountClaim(text);

    const name = ld.name || meta.title || titleOf(html) || null;

    let price = null;
    let priceSource = null;
    if (ld.price != null) { price = ld.price; priceSource = 'json-ld'; }
    else if (meta.price != null) { price = meta.price; priceSource = 'meta'; }
    else if (labelled.now != null) { price = labelled.now; priceSource = 'text-label'; }

    let wasPrice = null;
    let wasSource = null;
    if (ld.wasPrice != null) { wasPrice = ld.wasPrice; wasSource = 'json-ld'; }
    else if (meta.originalPrice != null) { wasPrice = meta.originalPrice; wasSource = 'meta'; }
    else if (strike) { wasPrice = strike.amount; wasSource = 'strikethrough'; }
    else if (labelled.rrp != null) { wasPrice = labelled.rrp; wasSource = 'rrp'; }
    else if (labelled.was != null) { wasPrice = labelled.was; wasSource = 'was-label'; }
    // A crossed-out figure that the page itself calls "RRP" is an RRP claim,
    // which is the weakest kind of reference price.
    if (wasPrice != null && labelled.rrp != null && Math.abs(labelled.rrp - wasPrice) < 0.005) wasSource = 'rrp';

    if (price == null && wasPrice != null) {
        if (labelled.save != null && labelled.save < wasPrice) {
            price = round2(wasPrice - labelled.save);
            priceSource = 'derived-save';
        } else {
            const wasIndex = labelled.wasIndex ?? moneys.find((x) => Math.abs(x.amount - wasPrice) < 0.005)?.index ?? 0;
            const below = moneys.filter((x) => x.amount < wasPrice);
            below.sort((a, b) => Math.abs(a.index - wasIndex) - Math.abs(b.index - wasIndex));
            if (below.length) { price = below[0].amount; priceSource = 'text-nearby'; }
        }
    }
    if (price == null && moneys.length) { price = moneys[0].amount; priceSource = 'text-first'; }

    // A "was" price that isn't higher than the price is not a discount.
    if (wasPrice != null && price != null && wasPrice <= price * 1.005) { wasPrice = null; wasSource = null; }

    // "30% off" with no visible reference price still implies one.
    if (wasPrice == null && claim.pct && !claim.upTo && price != null) {
        wasPrice = round2(price / (1 - claim.pct / 100));
        wasSource = 'derived';
    }

    const currency = ld.currency || meta.currency || strike?.currency || moneys.find((x) => x.currency)?.currency
        || (CODE_IN_TEXT.exec(text) || [])[1] || null;
    const discountPct = wasPrice != null && price != null ? Math.round((1 - price / wasPrice) * 100) : null;
    const signals = detectSignals(html, text);
    if (wasSource === 'rrp') signals.push({ kind: 'rrp-reference', evidence: 'Discount is measured against an RRP rather than a price this shop charged.' });

    return {
        name,
        currency,
        price,
        priceSource,
        wasPrice,
        wasSource,
        discountPct,
        claimedPct: claim.pct,
        upTo: claim.upTo,
        claimEvidence: claim.evidence,
        savings: labelled.save,
        priceValidUntil: ld.priceValidUntil,
        availability: ld.availability,
        timerEnd: extractTimerEnd(html, now),
        structuredData: ld.found,
        signals,
        hasSaleClaim: (wasPrice != null && price != null && wasPrice > price) || claim.pct != null,
    };
}
