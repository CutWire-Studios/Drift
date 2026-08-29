# cgltf

Single-header glTF 2.0 loader used by Drift's face-prop (`.glb`) pipeline.

- Upstream: https://github.com/jkuhlmann/cgltf
- Tag: `v1.15`
- Commit: `360db1a95480fe102ae9c69b27c5d101167ff5ba`
- License: MIT (see `LICENSE`)

Vendored in-tree rather than via FetchContent so Flatpak and offline builds do not need network
at configure time. To update: replace `cgltf.h` and `LICENSE` from the tagged release, then bump
the commit SHA recorded above.

Exactly one translation unit defines `CGLTF_IMPLEMENTATION`: `src/engine/ModelAsset.cpp`.
