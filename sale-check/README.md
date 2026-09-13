# Sale Check

Is that discount real? Paste a product page and Sale Check reads the price and
the claimed "was" price, remembers them, and flags the tricks fake sales rely
on: reference prices that were never charged, sales that never end, countdown
timers with no real deadline, and the usual "only 3 left" pressure.

It runs two ways with the same analysis code:

- **Web app** on Cloudflare Workers + Static Assets, with price history in KV.
- **CLI** for local use, with history in `~/.sale-check-history.json`. This is
  also the way round shops that block automated visits: save the page from your
  browser and pass the file.

## How it decides

A single visit can only read what the page says about itself. The verdicts that
matter come from checking the same page repeatedly:

| Evidence | Effect |
|---|---|
| The "was" price was the actual price for 28+ days before the sale | Strongly genuine |
| The "was" price was charged for at least a week before the sale | Genuine |
| 28+ days of checks and the "was" price was never the price | Fake |
| On sale at every check for 28+ days | Fake (a sale that never ends is just the price) |
| The "was" price went up during the sale | Fake |
| Countdown deadline moved later with no price change | Fake |
| Countdown computed from the visitor's own clock (inline script) | Fake |
| Discount measured against an RRP | Weak reference price |
| "% off" claim with no reference price shown | Weak reference price |
| 70%+ / 90%+ off | Implausible |
| Countdown text, "flash sale", scarcity counters, "N people viewing", "up to X% off" | Pressure tactics |

Each finding adds or subtracts suspicion points. The verdict is one of: No sale
to check, Looks genuine, Probably genuine, Unverified, Some warning signs,
Suspicious, Very likely fake. Confidence reflects how much history exists
(none / low / medium / high at 0, under 7, under 28, 28+ days).

The 28-day threshold follows the long-standing UK rule of thumb for reference
pricing (the CMA expects a "was" price to have been charged for a meaningful
period, typically at least as long as the sale). It is a heuristic, not legal
advice.

## Local use

```bash
cd sale-check
npm test
node cli.js https://www.example.com/product/123
node cli.js https://www.example.com/product/123 --html saved-page.html   # for sites that block bots
node cli.js https://www.example.com/product/123 --json
```

Re-run the same URL over the following days and weeks. The verdict only gets
stronger with repeat checks.

## Deploy (Cloudflare)

Follows the shared site standard: one `wrangler.jsonc`, production at the top
level, a named `env.staging`, `main` + `staging` branches.

```bash
cd sale-check
npm install
npx wrangler kv namespace create PRICE_HISTORY
npx wrangler kv namespace create PRICE_HISTORY --env staging
# paste the two ids into wrangler.jsonc, then choose a domain and uncomment "routes"
npm run deploy:staging
npm run deploy
```

`npm run dev` serves the site locally with a simulated KV. Each deploy stamps
`public/build.json` (build number = commit count, short SHA, UTC timestamp),
shown in the site footer and at `/api/version`.

Security headers (CSP, HSTS, nosniff, DENY framing, referrer and permissions
policies) are set on every response by the Worker. `/api/check` only fetches
public http(s) hosts.

## Limits

- Pages that draw prices with JavaScript, or that block non-browser visits,
  come back as "no price found". Use the CLI with `--html`.
- History is per normalised URL. The same product at two URLs is two histories.
- No rate limiting on `/api/check` yet; add Cloudflare rate-limiting rules or a
  Turnstile challenge before making it public.
