# Marketplace backend — briefing

Drift (the video editor) talks only to **`https://market.cutwire.org/api/v1`**. It has no Pexels / Unsplash / YouTube / Pixabay adapters. If a source exists, a quota changes, or a type (e.g. greenscreen) appears, that is **your config** — not a Drift release.

Full contract: [README.md](README.md) and [openapi.yaml](openapi.yaml). Extras (`drift-addons.cutwire.org`) is a different product; do not merge it into this API.

## What you own

1. **Catalog** — which media types and providers the app shows.
2. **Search proxy** — query the real store, return a uniform listing (opaque ids, your thumbs/previews).
3. **Download jobs** — fetch from the store, normalize to a clean mp4 / wav-or-aac / jpeg-png-webp, hand Drift a short-lived file URL.
4. **Per-client quotas** — e.g. 1 YouTube download / day / machine, without requiring an account.
5. **Website** — register / login / later coins. Drift never shows Sign in or your URL.

## Client identity (every `/api/v1` call)

Official Drift builds HMAC-sign requests with a shared key (`DRIFT_MARKET_CLIENT_KEY`). The env value is used as the HMAC key bytes with no decoding — a 64-character hex string is still 64 bytes, not 32. Same key is used to derive a stable `client_id` from a machine fingerprint, so wiping app data does not mint a new quota.

Required headers: `X-Cutwire-Client`, `X-Cutwire-Timestamp`, `X-Cutwire-Nonce`, `X-Cutwire-Signature`, `X-Cutwire-App`. Canonical string and verification rules are in the README. Treat HMAC as a speed bump, not DRM (GPLv3 rebuilds can extract the key) — also cap by IP.

Optional: `Authorization: Bearer <access_token>` after the user links a device from the website.

Do **not** require HMAC on the CDN file URL you return when a download job is ready.

## Endpoints to implement

| Method | Path | Role |
|---|---|---|
| `GET` | `/catalog` | Types + nested providers, filters, optional quota |
| `GET` | `/search` | `type`, `provider`, `q`, `limit`, `cursor` + declared filters |
| `GET` | `/items/{id}` | Listing detail + optional variants |
| `POST` | `/resolve` | Start a pasted-URL lookup; returns a job |
| `GET` | `/resolve/{id}` | Poll until `ready` / `failed`; ready jobs carry the listing |
| `POST` | `/downloads` | Start job; consume quota (and later coins) **here** |
| `GET` | `/downloads/{id}` | Poll until `ready` / `failed`; refund quota on failure |
| `POST` | `/auth/token` | Exchange website redirect `code` (binds account ↔ client id) |
| `POST` | `/auth/refresh` | Rotate access token |
| `POST` | `/auth/logout` | Invalidate refresh token |
| `GET` | `/me` | Account when Bearer is valid; **401 is normal** when anonymous |

Images may return `status: ready` on the POST. YouTube / large video stay `queued` → `processing` → `ready`.

## Product rules Drift already follows

- Catalog is the only list of types/providers. No hardcoded fallback.
- `price_coins` omitted or `0` = free. **Do not send an `is_free` flag.** Drift never shows “Free”.
- `price_coins > 0` → Drift shows the number only. No purchase UI in the app.
- Error `detail` is shown verbatim. Never put `https://market.cutwire.org` or “sign in” in it.
- Stable error `code` values: `invalid_client`, `rate_limited`, `auth_required`, `payment_required`, `provider_unavailable`, `not_found`, `download_failed`.
- Suggested type ids: `video`, `photo`, `audio`, `greenscreen` (`delivery: media`). Effects / face-prop **packs** stay on Extras.

## Website connect flow (no in-app button)

User logs in on the site, clicks something like “Connect Drift”, you redirect to:

```
cutwire://market/auth/callback?code=…&state=…
https://market.cutwire.org/app/auth/callback?code=…
```

Drift redeems `code` at `POST /auth/token` **with the signed client headers**. After that, Settings can show “connected” and Disconnect only.

## v1 vs later

**Now:** all listings can be free; accounts optional; coin field present; auth endpoints live.

**Later:** paid providers or per-item coins, quota differences for linked accounts. No payment handling inside Drift.

## Thumbnails and previews

Serve or proxy them from the market. Do not give Drift raw provider URLs that need API keys.
