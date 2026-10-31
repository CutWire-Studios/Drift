# Marketplace (client notes)

The API lives at `https://cutwire.studio/api/v1/marketplace`. The spec is `backend/openapi.json` in the CutWire-Studio repository, with prose in `docs/marketplace.md` there. This repository does not carry a copy.

Requests are HMAC-signed with `MarketIdentity` and sent through `MarketTransport`.

Client code map:

- `src/app/online/marketplace/*`: manifest, item loading, installs, source browser, transport
- `src/app/online/market/*`: client identity and request signing
- `src/app/online/downloads/*`: download jobs
- `src/qml/components/marketplace/*`, `src/qml/MarketplaceWindow.qml`, `src/qml/components/android/pages/AndroidMarketplacePage.qml`: UI
- `src/app/mcp/tools/McpMarket.cpp`: the `market_*` MCP tools

A layout section type the client doesn't know is skipped.

A bundled default manifest lives in `resources/marketplace/default-manifest.json`.
