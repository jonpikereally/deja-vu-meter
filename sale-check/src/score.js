// Turns what the page says (extract.js) and what we have seen before
// (history.js) into a verdict a shopper can act on. Points are "suspicion":
// warning signs add, verified facts subtract. The thresholds are deliberately
// conservative about calling something genuine, because a single page view
// can never prove that a reference price was really charged.

const SYMBOLS = { GBP: '£', EUR: '€', USD: '$', AUD: 'A$', CAD: 'CA$', NZD: 'NZ$' };

export const VERDICT_LABELS = {
    'no-sale': 'No sale to check',
    genuine: 'Looks genuine',
    'probably-genuine': 'Probably genuine',
    unverified: 'Unverified',
    warning: 'Some warning signs',
    suspicious: 'Suspicious',
    fake: 'Very likely fake',
};

export function formatMoney(amount, currency) {
    if (amount == null) return '?';
    const sym = SYMBOLS[currency];
    const num = amount.toFixed(2);
    return sym ? `${sym}${num}` : currency ? `${num} ${currency}` : num;
}

const days = (d) => (d >= 1 ? `${Math.round(d)} day${Math.round(d) === 1 ? '' : 's'}` : 'less than a day');

export function assess(extract, summary = { observations: 0, spanDays: 0 }, opts = {}) {
    const now = opts.now ?? Date.now();
    const findings = [];
    let score = 0;
    const add = (severity, title, detail, points = 0) => {
        findings.push({ severity, title, detail, points });
        score += points;
    };

    const { price, wasPrice, discountPct, claimedPct, upTo, currency, signals = [] } = extract;
    const money = (v) => formatMoney(v, currency);
    const claimsSale = (wasPrice != null && price != null && wasPrice > price) || claimedPct != null;
    const obs = summary.observations || 0;
    const span = summary.spanDays || 0;

    if (price == null) {
        add('info', 'No price found on the page',
            'The shop may draw prices with JavaScript or block automated visits. Save the page from your browser and check the saved file instead.');
    }

    if (!claimsSale) {
        add('info', 'No discount claimed',
            price != null
                ? `The page shows ${money(price)} with no reference price or percentage off, so there is no sale claim to verify. The price has been recorded for future comparisons.`
                : 'Nothing on the page reads as a sale claim.');
    } else {
        // What kind of reference price is this?
        if (extract.wasSource === 'derived') {
            add('amber', 'Discount claimed without a visible reference price',
                `The page says "${claimedPct}% off" but shows no crossed-out or "was" price. The implied reference price is ${money(wasPrice)}.`, 10);
        } else if (extract.wasSource === 'rrp') {
            add('amber', 'Discount measured against an RRP',
                'Recommended retail prices are set by the manufacturer and are often never charged by anyone. A discount against an RRP tells you little about what this shop normally charges.', 8);
        } else if (wasPrice != null) {
            add('info', 'Reference price found',
                `The page claims this was ${money(wasPrice)} and is now ${money(price)} (${discountPct}% off).`);
        }
        if (upTo && claimedPct != null) {
            add('info', '"Up to" discount',
                'An "up to X% off" banner applies to the best case across a range, not necessarily to this item.', 3);
        }
        if (discountPct != null && discountPct >= 90) {
            add('amber', 'Implausibly deep discount', `${discountPct}% off is rarely genuine outside clearance. Treat the reference price as decorative until the history proves otherwise.`, 15);
        } else if (discountPct != null && discountPct >= 70) {
            add('amber', 'Very deep discount', `${discountPct}% off is unusual for a live product line. Check that the reference price was ever really charged.`, 10);
        }

        // What does the history say about the reference price?
        if (obs < 2 || span < 1) {
            add('info', 'No price history yet',
                'This is the first time this page has been checked. Come back in a few days and again in a few weeks: the verdict only gets stronger with repeat checks.');
        } else if (summary.daysAtReferenceBeforeSale >= 28) {
            add('green', 'Reference price verified',
                `${money(summary.referencePrice)} was the actual price for ${days(summary.daysAtReferenceBeforeSale)} immediately before this sale started. That is what a genuine reference price looks like.`, -30);
        } else if (summary.daysAtReferenceBeforeSale >= 7) {
            add('green', 'Reference price was charged before the sale',
                `${money(summary.referencePrice)} was the actual price for ${days(summary.daysAtReferenceBeforeSale)} before the sale. UK guidance expects a reference price to have been charged for a meaningful period, typically longer than the sale itself.`, -15);
        } else if (summary.referenceEverCharged) {
            add('green', 'Reference price has been charged at some point',
                `${money(summary.referencePrice)} was seen as the actual price during the ${days(span)} this page has been watched, though not for long immediately before the sale.`, -10);
        } else if (span >= 28) {
            add('red', 'Reference price never charged',
                `In ${days(span)} of watching (${obs} checks) this page has never sold at ${money(summary.referencePrice)}. A "was" price that was never the price is a fake discount.`, 30);
        } else if (span >= 7) {
            add('amber', 'Reference price not seen as the real price yet',
                `Across ${days(span)} (${obs} checks) the page has not sold at ${money(summary.referencePrice)}. Keep checking; after four weeks this becomes a strong signal.`, 10);
        } else {
            add('info', 'Not enough history yet',
                `Only ${days(span)} of history so far (${obs} checks). Check again over the coming weeks.`);
        }

        if (summary.currentRunDays >= 28 && summary.saleFraction === 1) {
            add('red', 'Perpetual sale',
                `This page has been "on sale" for every one of ${obs} checks over ${days(summary.currentRunDays)}. A sale that never ends is just the price.`, 35);
        } else if (summary.currentRunDays >= 14) {
            add('amber', 'Long-running sale',
                `The current sale has run for at least ${days(summary.currentRunDays)}.`, 15);
        }
        if (summary.referencePriceRose) {
            add('red', 'The "was" price went up during the sale',
                'Earlier checks showed a lower reference price for the same item. Inflating the "was" figure to make the discount look bigger is a textbook fake sale.', 25);
        }
        if (summary.saleCycles >= 2) {
            add('amber', 'Yo-yo pricing',
                `The item has gone on sale ${summary.saleCycles} separate times while being watched. Briefly returning to the higher price to "reset" a sale is a known trick.`, 10);
        }
    }

    if (summary.timerRestarts > 0) {
        add('red', 'Countdown restarted',
            `The advertised deadline has moved later ${summary.timerRestarts} time${summary.timerRestarts === 1 ? '' : 's'} with no change in price. The clock is a prop.`, 30);
    }
    if (obs >= 2 && summary.minPrice != null && price != null && summary.minPrice < price * 0.98) {
        add('info', 'Seen cheaper before',
            `This page sold at ${money(summary.minPrice)} on ${new Date(summary.lowestSeenAt).toDateString()}. Today's "sale" price is higher than that.`, 5);
    }

    for (const s of signals) {
        switch (s.kind) {
            case 'evergreen-timer':
                add('red', 'Fake countdown timer', `${s.evidence} (${s.code})`, 30); break;
            case 'countdown':
                add('amber', 'Countdown or deadline pressure', `"${s.evidence}". Deadlines are legitimate when they are real; check back after it expires.`, 10); break;
            case 'flash-sale':
                add('amber', 'Limited-time wording', `"${s.evidence}"`, 5); break;
            case 'scarcity':
                add('amber', 'Scarcity claim', `"${s.evidence}". Stock counters on retail sites are frequently fictional; reload later and see if the number changes.`, 8); break;
            case 'social-proof':
                add('amber', 'Live-activity claim', `"${s.evidence}". "X people are viewing" counters are usually random numbers.`, 8); break;
            case 'up-to':
                if (!upTo) add('info', '"Up to" discount banner', `"${s.evidence}"`, 3);
                break;
            default: break;
        }
    }

    if (extract.priceValidUntil) {
        const until = Date.parse(extract.priceValidUntil);
        if (Number.isFinite(until) && until < now - 86400000) {
            add('amber', 'Stale offer data', `The page's structured data says this price was valid until ${new Date(until).toDateString()}, yet it is still being shown.`, 5);
        }
    }

    score = Math.max(0, Math.min(100, score));

    let verdict;
    if (!claimsSale && score < 15) verdict = 'no-sale';
    else if (score <= 10 && summary.daysAtReferenceBeforeSale >= 28) verdict = 'genuine';
    else if (score <= 15 && span < 7) verdict = 'unverified';
    else if (score <= 15) verdict = 'probably-genuine';
    else if (score < 40) verdict = 'warning';
    else if (score < 65) verdict = 'suspicious';
    else verdict = 'fake';

    const confidence = obs < 2 ? 'none' : span < 7 ? 'low' : span < 28 ? 'medium' : 'high';

    return {
        verdict,
        label: VERDICT_LABELS[verdict],
        score,
        confidence,
        summary: summarySentence(verdict, confidence, claimsSale),
        findings: findings.sort((a, b) => rank(b.severity) - rank(a.severity)),
    };
}

const rank = (s) => ({ red: 3, amber: 2, green: 1, info: 0 })[s] ?? 0;

function summarySentence(verdict, confidence, claimsSale) {
    switch (verdict) {
        case 'no-sale': return 'Nothing on this page claims a discount. Its price has been recorded so a future sale can be checked against it.';
        case 'genuine': return 'The reference price was really charged for a sustained period before the sale and the page is not using pressure tactics.';
        case 'probably-genuine': return 'No red flags, and the history so far is consistent with a real discount.';
        case 'unverified': return 'The page itself looks clean, but a single visit cannot prove the reference price was ever charged. Check again over the next few weeks.';
        case 'warning': return 'The discount may be real, but the page shows patterns that fake sales rely on. Compare prices elsewhere before trusting the saving.';
        case 'suspicious': return 'Several signs point to a manufactured discount. Assume the "saving" is not real until independent price history says otherwise.';
        case 'fake': return 'This has the hallmarks of a fake sale: the reference price is not credible and the page is manufacturing urgency.';
        default: return claimsSale ? '' : '';
    }
}

// Independent sources a shopper can use to check the price themselves.
export function nextSteps(url, name) {
    const steps = [];
    const q = encodeURIComponent((name || '').slice(0, 120));
    let host = '';
    try { host = new URL(url).hostname; } catch { /* leave blank */ }
    if (/(^|\.)amazon\./i.test(host)) {
        const asin = /\/(?:dp|gp\/product)\/([A-Z0-9]{10})/i.exec(url);
        steps.push({ label: 'Amazon price history on camelcamelcamel', href: asin ? `https://camelcamelcamel.com/product/${asin[1]}` : `https://camelcamelcamel.com/search?sq=${encodeURIComponent(url)}` });
    }
    if (q) {
        steps.push({ label: 'Compare prices on PriceSpy', href: `https://pricespy.co.uk/search?search=${q}` });
        steps.push({ label: 'Compare prices on PriceRunner', href: `https://www.pricerunner.com/results?q=${q}` });
        steps.push({ label: 'Search Google Shopping', href: `https://www.google.com/search?tbm=shop&q=${q}` });
    }
    steps.push({ label: 'See what this page said before (Wayback Machine)', href: `https://web.archive.org/web/*/${url}` });
    return steps;
}
