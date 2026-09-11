# Cutwire Marketplace API

Contract for `https://market.cutwire.org/api/v1`. Drift consumes this API; it never talks to Pexels, Unsplash, YouTube, Pixabay, or any other source directly. Types, providers, quotas, and prices are **backend configuration**. Enabling a source or changing a daily cap does not require a Drift release.

The OpenAPI description is [openapi.yaml](openapi.yaml). This document is the architecture and the rules that YAML cannot say clearly.

The marketplace website and the API implementation live outside this repository. Extras (`drift-addons.cutwire.org`) stays a separate signed-pack installer.

## Design goals

- The client is store-agnostic. It renders whatever `GET /catalog` returns.
- Anonymous use is the default. Accounts exist for later paid quotas and coin-priced items.
- Every request is tied to a stable, signed client identity so the backend can rate-limit per machine (for example one YouTube download per day) without an account.
- Drift never shows Sign in, Create account, or `market.cutwire.org`. Informed users connect a client from the website via a redirect into the app.
- Free listings are unmarked. Coin prices appear only when `price_coins > 0`.

## Base URL and versioning

```
https://market.cutwire.org/api/v1
```

JSON request and response bodies are UTF-8 `application/json`. File bytes travel through a short-lived URL on the download job, not through JSON. Breaking changes get `/api/v2`. Additive fields are allowed in v1; clients ignore unknown keys.

## Client identity

The HMAC key ships in official Drift binaries (`DRIFT_MARKET_CLIENT_KEY`). It is **not** a user secret: anyone who rebuilds from source can extract it. Its job is to raise the bar above deleting app data or editing a JSON file. Treat it as a mild obstacle, and keep IP / account-level backstops for abuse.

### Key encoding

`key` is the bytes of the configured `DRIFT_MARKET_CLIENT_KEY` string, with no interpretation — a 64-character hex value is 64 ASCII bytes, not the 32 bytes it encodes. Nothing on either side decodes it. The service hex-decoded it once, which made a hex-looking key silently a different key than the client signed with, and the API can only report that as `invalid_client`.

### Client id

```
material   = "cutwire-market-id-v1|" + platform + "|" + fingerprint
client_id  = lowercase hex( HMAC-SHA256(key, UTF-8(material)) )
```

`platform` is one of `linux`, `windows`, `macos`, `android`.

`fingerprint` is a stable machine+user string. It is **not** a random UUID in app data — wiping config must not mint a new quota. Drift hashes it with the key, so the server never sees raw hardware ids.

| OS | Fingerprint |
|---|---|
| Linux | contents of `/etc/machine-id` (trim) + `\|` + numeric uid |
| Windows | `HKLM\SOFTWARE\Microsoft\Cryptography\MachineGuid` + `\|` + user name |
| macOS | `kern.uuid` + `\|` + user name |
| Android | `Settings.Secure.ANDROID_ID` (already per app-signing-key + user + device) |

**Flatpak:** the sandbox may hide the host `/etc/machine-id`. The id is then stable for that install, but deleting the Flatpak data can mint a new one. Combine with IP limits.

Changing `/etc/machine-id` (or equivalent) also mints a new id without a rebuild. That is expected; do not treat HMAC as DRM.

### Request signing

Every call to `/api/v1` carries:

| Header | Value |
|---|---|
| `X-Cutwire-Client` | `client_id` |
| `X-Cutwire-Timestamp` | Unix seconds, decimal, no leading zeros |
| `X-Cutwire-Nonce` | 32 lowercase hex chars (16 random bytes) |
| `X-Cutwire-Signature` | lowercase hex HMAC-SHA256 of the canonical string |
| `X-Cutwire-App` | `Drift/<version> (<os>)` |
| `Authorization` | `Bearer <access_token>` when the device is linked; omit otherwise |

Canonical string, UTF-8, `\n` separators, no trailing newline:

```
{timestamp}\n{nonce}\n{client_id}\n{METHOD}\n{pathAndQuery}\n{bodySha256Hex}
```

- `METHOD` is uppercase (`GET`, `POST`).
- `pathAndQuery` is the request target: path starting with `/`, plus `?` and the query string **exactly as sent**. No scheme, host, or fragment. Example: `/api/v1/search?type=video&provider=pexels&q=ocean`.
- `bodySha256Hex` is lowercase hex SHA-256 of the raw body bytes. An empty body still hashes (SHA-256 of zero bytes is `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`).

Verify: HMAC with the same key, timestamp within ±300 seconds, nonce unused in that window, `X-Cutwire-Client` matches the id inside the canonical string. Reject missing headers with `401` and `code: invalid_client`.

Do **not** require these headers on short-lived file URLs handed back in a download job. Those may live on a CDN with their own signature.

## Catalog

`GET /catalog` is the only list of types and providers the client is allowed to know. Hide YouTube, add Pixabay, introduce `greenscreen`, or change a quota here.

- `delivery`: `media` means Drift imports the file into the media bin. Reserve `file` for later non-bin assets (for example a raw `.glb`) without teaching Drift new stores. Drift v1 shows types with `delivery: media` only.
- `media_kind`: `video` | `audio` | `image`. Greenscreen clips are a separate `id` (e.g. `greenscreen`) with `media_kind: video`.
- No `free` flag. A type or provider that should not appear is omitted.
- `filters` is a closed set (`enum`, `toggle`, `text`) so the client can render them without per-store widgets. Search sends each filter `id` as a query parameter.
- `quota` is informational. Enforcement is on `POST /downloads`. Omit `quota` when unlimited.

Suggested type ids: `video`, `photo`, `audio`, `greenscreen`. Video/audio effects and 3D face-prop **packs** stay on Extras. If the market later hosts clips or models as files, add types then.

## Search and items

`GET /search?type=…&provider=…&q=…&limit=30&cursor=…` plus declared filter ids.

Item ids are opaque. Drift treats them as strings and never parses provider prefixes.

`thumb_url` and `preview_url` are market-hosted or proxied and short-lived. Drift must not be given raw Pexels/YouTube URLs that need API keys.

**Pricing**

- `price_coins` omitted or `0` → free. The client shows **no** “Free” badge.
- `price_coins > 0` → show that coin amount only.
- `downloadable: false` when this client cannot take the item (quota exhausted, priced and anonymous, …). Still list it. A download attempt then fails with a typed error.

`GET /items/{id}` returns the same object plus optional `variants`.

### Direct-link providers

Some sources have no browsable catalog — YouTube, YouTube Music, Instagram,
Facebook, TikTok, Vimeo, X, and SoundCloud only make sense when the user pastes
a link. Those providers appear in the catalog with `capabilities: ["resolve"]`
and return nothing from `/search`.

`POST /resolve` with `{ "url": "…" }` starts a job (`201`). Poll
`GET /resolve/{id}` until `ready` or `failed` — the same shape as downloads.
A ready job carries a normal `Item`. Its `id` goes straight to `POST /downloads`
like any other listing, so nothing downstream is special-cased.

A link nobody owns is `404` `not_found` on the POST. A link that *is* recognised
but whose source this deployment cannot serve — the extractor is missing, or an
operator turned the provider off — is `503` `provider_unavailable` on the POST,
because the two are different problems and a client that retries later is right
about the second and wrong about the first. A recognised link that then times
out or fails during extraction lands on the job as `failed` with
`provider_unavailable` / `source_timeout` (or the matching item `not_found`
reason), so the client is not left with a bare HTTP timeout.

Clients that only implement `/search` simply see these providers as empty; they
are never a hard dependency.

## Downloads

Backend fetches from the source, normalizes to a clean file, and returns that file. Drift only downloads the result.

Preferred containers: `video/mp4`, `audio/wav` or `audio/aac`, `image/jpeg` / `image/png` / `image/webp`.

`POST /downloads` with `{ "item_id", "variant_id"? }` starts a job (`201`). Images may complete in that response (`status: ready`). YouTube and large videos stay `queued` / `processing`; the client polls `GET /downloads/{id}`.

Consume quota (and coins, later) at job **create**, not on poll. Failed jobs refund.

When `ready`, `file.url` is a short-lived GET. `file.sha256` is lowercase hex; Drift verifies when present.

## Errors

RFC 7807 `application/problem+json` plus a stable `code` Drift switches on:

| code | HTTP | Drift copy (no URLs, no Sign in) |
|---|---|---|
| `invalid_client` | 401 | Could not reach the marketplace. |
| `rate_limited` | 429 | Daily limit reached for this source. Try again later. |
| `auth_required` | 401 or 403 | This item needs a connected account. |
| `payment_required` | 402 | Not enough coins. |
| `provider_unavailable` | 503 | This source is temporarily unavailable. |
| `not_found` | 404 | That item is no longer available. |
| `download_failed` | 422 or 500 | Could not prepare that file. |

Include `Retry-After` and/or `reset_at` on 429. Do not put `https://market.cutwire.org` in `detail` — Drift may show that string.

### `reason`

Responses may also carry a `reason`: a finer-grained cause than `code`, because one code has to cover failures that need different words — a removed video and a region-locked one are both `not_found`, and telling them apart is the difference between "try another link" and "this will never work here".

`reason` is **additive and optional**. Every value maps onto one of the seven frozen codes, so a client that ignores it keeps working, and an unrecognised value must be treated as the bare `code`. Never switch on it without a fallback.

| `code` | `reason` values |
|---|---|
| `provider_unavailable` | `source_blocked` (the source refused *us*, not the item), `source_unreachable`, `source_timeout`, `source_disabled`, `source_error` |
| `not_found` | `item_removed`, `item_private`, `item_geoblocked`, `item_missing`, `link_no_media`, `link_unsupported` |
| `download_failed` | `file_too_large`, `live_stream`, `download_error` |

`detail` is still the sentence to show. `reason` is for deciding whether a retry is worth offering at all.

## Optional account

Anonymous signed identity is enough for free downloads the backend allows.

The website (not Drift) owns register, login, and later coin purchase. “Connect Drift” on the site redirects to:

```
cutwire://market/auth/callback?code=…&state=…
https://market.cutwire.org/app/auth/callback?code=…&state=…
```

Drift exchanges `code` at `POST /auth/token` **with the signed client headers**, so the account binds to that device. Response: `access_token`, `refresh_token`, `expires_in`, `account`.

`POST /auth/refresh` and `POST /auth/logout` follow. `GET /me` returns the account when the Bearer token is valid, and `401` when anonymous (normal — not an error toast).

When a Bearer token is present, apply quotas to the **account** if you want paid users to share quota across devices; otherwise keep using `client`. Document the choice in your backend config.

### Website checklist

1. User logs in on the website.
2. “Connect Drift” issues a one-time `code` and redirects to the callback URL.
3. Drift redeems the code; you store `account_id ↔ client_id`.
4. Later: coin balance, paid providers, per-download prices. No payment UI in Drift.

## Client UX rules (Drift)

- No Sign in, Create account, or marketplace URL anywhere in the app.
- Settings may show “Marketplace account connected” and Disconnect only after a successful callback.
- Toast on successful link: “Marketplace account connected.”
- Never advertise free items as free.
- Do not hardcode provider names as a fallback when the catalog request fails.

## Abuse notes for backend implementers

HMAC + stable fingerprint stops casual identity spinning. It does not stop a modified binary. Also:

- Cap requests per IP as well as per `client_id`.
- Bound concurrent download jobs per client.
- Expire file URLs quickly.
- Log `X-Cutwire-App` for crash/version correlation, not as an access control.
