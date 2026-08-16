# Unreleased changes

Tracks work done on `main` **since the last public release**. Use this to see what is already fixed or added before filing an issue. Cleared when a new release ships.

**Last released version:** `0.2.2`

---

## Added

- macOS support: the build produces a `Drift.app` bundle, `scripts/package-macos.sh` writes a
  signed `.dmg`, and CI publishes an Apple Silicon disk image alongside the other platforms. See
  the [macOS section of the README](README.md#macos) for prerequisites and known limits.

## Fixed

- macOS: the video preview stayed black. `NSOpenGLContext` will not share objects between the
  legacy 2.1 context Qt defaults to and the core 3.3 one the compositor uses, so the composited
  texture was never valid in the scene graph.
