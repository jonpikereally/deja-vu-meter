// Price history for one product page. A single visit can only read what the
// page says about itself; the verdicts that matter ("this has been on sale
// for six weeks", "the was-price was never charged") come from comparing
// snapshots over time. Everything here is pure so the Worker, the CLI and the
// tests share it.

const DAY = 86400000;
export const MAX_SNAPSHOTS = 400;

// Repeat checks of the same page within this window add no information.
const DEDUPE_WINDOW = 6 * 3600000;

const TRACKING_PARAM = /^(utm_.*|fbclid|gclid|dclid|msclkid|mc_[ce]id|_ga|_gl|igshid|ref|ref_|tag|si|srsltid|yclid|ttclid|twclid)$/i;

// The key under which a page's history is stored. Tracking parameters and
// fragments never change which product you are looking at.
export function normaliseUrl(input) {
    const u = new URL(input);
    u.hash = '';
    u.hostname = u.hostname.toLowerCase().replace(/^www\./, '');
    for (const k of [...u.searchParams.keys()]) if (TRACKING_PARAM.test(k)) u.searchParams.delete(k);
    u.searchParams.sort();
    if (u.pathname.length > 1 && u.pathname.endsWith('/')) u.pathname = u.pathname.slice(0, -1);
    return u.toString();
}

export function makeSnapshot(extract, ts = Date.now()) {
    return {
        ts,
        price: extract.price ?? null,
        wasPrice: extract.wasPrice ?? null,
        wasSource: extract.wasSource ?? null,
        currency: extract.currency ?? null,
        timerEnd: extract.timerEnd ?? null,
    };
}

export function addSnapshot(history, snap) {
    const list = Array.isArray(history) ? history.slice() : [];
    const last = list[list.length - 1];
    const unchanged = last && last.price === snap.price && last.wasPrice === snap.wasPrice && last.timerEnd === snap.timerEnd;
    if (unchanged && snap.ts - last.ts < DEDUPE_WINDOW) return list;
    list.push(snap);
    if (list.length > MAX_SNAPSHOTS) list.splice(0, list.length - MAX_SNAPSHOTS);
    return list;
}

const onSale = (s) => s.wasPrice != null && s.price != null && s.wasPrice > s.price * 1.005;

export function summarise(history, now = Date.now()) {
    const list = (history || []).filter((s) => s && s.ts && s.price != null).sort((a, b) => a.ts - b.ts);
    const n = list.length;
    if (!n) return { observations: 0, spanDays: 0 };

    const first = list[0];
    const latest = list[n - 1];
    const spanDays = (now - first.ts) / DAY;
    const saleObservations = list.filter(onSale).length;

    // The current unbroken run of "on sale" snapshots, if the latest is on sale.
    let runStart = n - 1;
    const latestOnSale = onSale(latest);
    if (latestOnSale) while (runStart > 0 && onSale(list[runStart - 1])) runStart--;
    const currentRunDays = latestOnSale ? (now - list[runStart].ts) / DAY : 0;

    // Was the current "was" price ever the price actually charged, and for how
    // long immediately before the sale started?
    const ref = latestOnSale ? latest.wasPrice : null;
    let referenceEverCharged = false;
    let daysAtReferenceBeforeSale = 0;
    let referencePriceRose = false;
    if (ref != null) {
        const near = (s) => !onSale(s) && Math.abs(s.price - ref) <= ref * 0.02;
        referenceEverCharged = list.some(near);
        let i = runStart - 1;
        let start = null;
        while (i >= 0 && near(list[i])) { start = list[i].ts; i--; }
        if (start !== null) daysAtReferenceBeforeSale = (list[runStart].ts - start) / DAY;
        const earlierWas = list.slice(0, n - 1).filter(onSale).map((s) => s.wasPrice);
        if (earlierWas.length && Math.min(...earlierWas) < ref * 0.98) referencePriceRose = true;
    }

    // A deadline that moves later while nothing else changed is a timer that
    // restarted rather than a new sale.
    let timerRestarts = 0;
    for (let i = 1; i < n; i++) {
        const a = list[i - 1];
        const b = list[i];
        if (a.timerEnd && b.timerEnd && Date.parse(b.timerEnd) > Date.parse(a.timerEnd) + 3600000
            && a.price === b.price && a.wasPrice === b.wasPrice) timerRestarts++;
    }

    let saleCycles = 0;
    for (let i = 1; i < n; i++) if (onSale(list[i]) && !onSale(list[i - 1])) saleCycles++;

    const prices = list.map((s) => s.price);
    const minPrice = Math.min(...prices);
    const lowest = list.find((s) => s.price === minPrice);

    return {
        observations: n,
        spanDays,
        firstSeen: first.ts,
        lastSeen: latest.ts,
        latestOnSale,
        saleObservations,
        saleFraction: saleObservations / n,
        currentRunDays,
        referencePrice: ref,
        referenceEverCharged,
        daysAtReferenceBeforeSale,
        referencePriceRose,
        timerRestarts,
        saleCycles,
        minPrice,
        maxPrice: Math.max(...prices),
        lowestSeenAt: lowest ? lowest.ts : null,
        currentPrice: latest.price,
    };
}
