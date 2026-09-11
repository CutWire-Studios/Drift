#!/usr/bin/env bash
# Builds Skia (Ganesh/GL + SkParagraph + Skottie + SVG) as static archives for one target and
# installs them into third_party/prebuilt/skia/<target>/{lib,include,SkiaConfig.cmake}, which is
# where cmake/FindSkia.cmake looks when DRIFT_WITH_SKIA is on.
#
# Why from source everywhere: Skia has no ABI stability and no official CMake build, Flatpak builds
# with no network, Android needs the same NDK Qt was built with, and the prebuilts that exist
# (vcpkg, JetBrains skia-pack) pick their own module set, ICU flavour and CRT. One pinned commit and
# one GN argument list here means every platform renders identically.
#
# SkiaConfig.cmake is generated from `gn desc` rather than written by hand because consumers must
# compile with exactly the defines Skia was built with (SK_R32_SHIFT decides SkColor byte order,
# SK_GANESH/SK_GL gate the GPU headers) — hand-maintaining that list is how ABI mismatches happen.
#
# Usage: third_party/build-skia.sh [target]
#   target  linux-x64 (default) | android-arm64-v8a | android-armeabi-v7a | android-x86_64
#           Android targets need ANDROID_NDK_ROOT, as build-android.sh does.
set -euo pipefail

TARGET="${1:-linux-x64}"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$HERE/src"
OUT="$HERE/prebuilt/skia/$TARGET"

# Milestone 148 — the same commit vcpkg's `skia` port pins, so a vcpkg build stays a drop-in
# alternative on Windows. Bump one milestone at a time; headers move between them.
SKIA_MILESTONE="m148"
SKIA_COMMIT="e7c90ecca9444fe09598f1630ab7cee2c0ee027a"
SKIA_SHA256="7b5158952ebc4e2ce8de19f036b7613e266705003469579d9e98070ead62fc3b"

# Modules Drift links. Order is dependency order (dependents first) because static archives are
# resolved left to right; FindSkia writes them into the imported target in this order.
SKIA_TARGETS=(
  //modules/skottie:skottie
  //modules/sksg:sksg
  //modules/skresources:skresources
  //modules/jsonreader:jsonreader
  //modules/svg:svg
  //modules/skparagraph:skparagraph
  //modules/skshaper:skshaper
  //modules/skunicode:skunicode_icu
  //modules/skunicode:skunicode_core
  //:skia
)

mkdir -p "$SRC" "$OUT"
JOBS="${JOBS:-3}"   # unbounded parallelism OOMs a 15 GB machine on Skia's larger TUs

# --- source ------------------------------------------------------------------
SKIA_SRC="$SRC/skia-$SKIA_COMMIT"
if [ ! -f "$SKIA_SRC/BUILD.gn" ]; then
  TARBALL="$SRC/skia-$SKIA_COMMIT.tar.gz"
  [ -f "$TARBALL" ] || curl -fL --retry 3 -o "$TARBALL" \
      "https://github.com/google/skia/archive/$SKIA_COMMIT.tar.gz"
  if [ "$SKIA_SHA256" != "__SKIA_SHA256__" ]; then
    echo "$SKIA_SHA256  $TARBALL" | sha256sum -c -
  else
    echo "NOTE: pin SKIA_SHA256=$(sha256sum "$TARBALL" | cut -d' ' -f1) in $0" >&2
  fi
  tar -xzf "$TARBALL" -C "$SRC"
fi

# gn is not packaged on every distro; Skia's fetch-gn downloads the exact binary its BUILD files
# were written for. A system gn wins when present (Flatpak builds it as its own module).
if command -v gn >/dev/null 2>&1; then
  GN="$(command -v gn)"
else
  [ -x "$SKIA_SRC/bin/gn" ] || ( cd "$SKIA_SRC" && python3 bin/fetch-gn )
  GN="$SKIA_SRC/bin/gn"
fi

# --- GN arguments --------------------------------------------------------------
# is_official_build turns off tools/tests and defaults every skia_use_system_* to true; the
# per-platform block below overrides those where the target has no system libraries.
COMMON_ARGS=(
  is_official_build=true
  is_component_build=false
  is_debug=false
  skia_use_gl=true
  skia_use_vulkan=false
  skia_use_metal=false
  skia_use_direct3d=false
  skia_use_dawn=false
  skia_enable_graphite=false
  skia_enable_ganesh=true
  skia_enable_skshaper=true
  skia_enable_skunicode=true
  skia_enable_skparagraph=true
  skia_enable_skottie=true
  skia_enable_svg=true
  skia_use_harfbuzz=true
  skia_use_icu=true
  skia_use_icu4x=false
  skia_use_libgrapheme=false
  skia_use_client_icu=false
  skia_use_freetype=true
  skia_use_expat=true
  skia_use_zlib=true
  # PNG decode only: CBDT emoji glyphs and Lottie/SVG embedded images are PNG. Nothing encodes.
  skia_use_libpng_decode=true
  skia_use_libpng_encode=false
  skia_use_libjpeg_turbo_decode=false
  skia_use_libjpeg_turbo_encode=false
  skia_use_libwebp_decode=false
  skia_use_libwebp_encode=false
  skia_use_wuffs=false
  skia_use_piex=false
  skia_use_dng_sdk=false
  skia_use_lua=false
  skia_enable_pdf=false
  skia_enable_tools=false
  skia_enable_gpu_debug_layers=false
  skia_enable_spirv_validation=false
  skia_enable_fontmgr_custom_empty=true
  skia_enable_fontmgr_custom_directory=true
)

case "$TARGET" in
  linux-x64)
    PLATFORM_ARGS=(
      cc=\"clang\"
      cxx=\"clang++\"
      target_cpu=\"x64\"
      skia_use_fontconfig=true
      skia_use_system_freetype2=true
      skia_use_system_harfbuzz=true
      skia_use_system_icu=true
      skia_use_system_expat=true
      skia_use_system_libpng=true
      skia_use_system_zlib=true
      extra_cflags=[\"-fPIC\"]
    )
    ;;
  android-*)
    : "${ANDROID_NDK_ROOT:?set ANDROID_NDK_ROOT to the NDK version Qt for Android was built against}"
    ABI="${TARGET#android-}"
    case "$ABI" in
      arm64-v8a)   GN_CPU=arm64 ;;
      armeabi-v7a) GN_CPU=arm ;;
      x86_64)      GN_CPU=x64 ;;
      *) echo "unsupported Android ABI: $ABI" >&2; exit 1 ;;
    esac
    # Same API floor as build-android.sh / minSdk. Bundled externals need a full DEPS sync.
    PLATFORM_ARGS=(
      ndk=\"$ANDROID_NDK_ROOT\"
      ndk_api=28
      target_os=\"android\"
      target_cpu=\"$GN_CPU\"
      skia_use_fontconfig=false
      skia_use_system_freetype2=false
      skia_use_system_harfbuzz=false
      skia_use_system_icu=false
      skia_use_system_expat=false
      skia_use_system_libpng=false
      skia_use_system_zlib=false
      extra_cflags=[\"-fPIC\"]
    )
    # tools/git-sync-deps has no filter and would clone every external (Dawn, Vulkan, ANGLE —
    # several GB). Hand it a DEPS file trimmed to the six libraries this configuration compiles.
    ( cd "$SKIA_SRC" && python3 - <<'PY'
scope = {}
scope["Var"] = lambda name: scope["vars"][name]
exec(open("DEPS").read(), scope)
keep = ("expat", "freetype", "harfbuzz", "icu", "libpng", "zlib")
sub = {k: v for k, v in scope["deps"].items() if k.rsplit("/", 1)[-1] in keep}
open("DEPS.drift", "w").write("deps = " + repr(sub) + "\n")
PY
      GIT_SYNC_DEPS_PATH="$SKIA_SRC/DEPS.drift" GIT_SYNC_DEPS_SKIP_EMSDK=1 python3 tools/git-sync-deps )
    ;;
  *) echo "unsupported target: $TARGET" >&2; exit 1 ;;
esac

BUILD="$SKIA_SRC/out/$TARGET"
GN_ARGS="${COMMON_ARGS[*]} ${PLATFORM_ARGS[*]}"
echo "==> gn gen $BUILD"
( cd "$SKIA_SRC" && "$GN" gen "$BUILD" --args="$GN_ARGS" )

echo "==> ninja -j$JOBS"
ninja -C "$BUILD" -j"$JOBS" "${SKIA_TARGETS[@]#//}"

# --- install -----------------------------------------------------------------
rm -rf "$OUT/lib" "$OUT/include"
mkdir -p "$OUT/lib" "$OUT/include"

# Skia headers include each other by source-root-relative path ("include/core/SkCanvas.h",
# "modules/skottie/include/Skottie.h", "modules/skcms/skcms.h"), so the install keeps that tree
# with $OUT/include as the root. Only headers; src/ stays private.
( cd "$SKIA_SRC" && find include modules -name '*.h' -print0 ) \
  | ( cd "$SKIA_SRC" && tar --null -cf - -T - ) | tar -xf - -C "$OUT/include"

LIBS=()
for t in "${SKIA_TARGETS[@]}"; do
  name="${t##*:}"
  [ -f "$BUILD/lib$name.a" ] || { echo "missing $BUILD/lib$name.a" >&2; exit 1; }
  cp "$BUILD/lib$name.a" "$OUT/lib/"
  LIBS+=("lib$name.a")
done
# Archives the targets above pull in transitively (skcms, bentleyottmann, bundled externals).
for a in "$BUILD"/*.a; do
  b="$(basename "$a")"
  case " ${LIBS[*]} " in *" $b "*) ;; *) cp "$a" "$OUT/lib/"; LIBS+=("$b") ;; esac
done

# Defines and system libraries as GN resolved them, unioned over every target. The
# *_IMPLEMENTATION defines must not leak: they flip SK_API to dllexport-style visibility. NDEBUG
# becomes SK_RELEASE: Skia derives SK_DEBUG from NDEBUG's absence, and a Debug build of Drift
# compiling the headers as SK_DEBUG against a release archive changes struct layouts.
DEFINES="$(cd "$SKIA_SRC" && for t in "${SKIA_TARGETS[@]}"; do "$GN" desc "$BUILD" "$t" defines; done \
           | grep -v '_IMPLEMENTATION' | sed 's/^NDEBUG$/SK_RELEASE/' | sort -u)"
SYSLIBS="$(cd "$SKIA_SRC" && for t in "${SKIA_TARGETS[@]}"; do "$GN" desc "$BUILD" "$t" libs 2>/dev/null || true; done \
           | sort -u)"

{
  echo "# Generated by third_party/build-skia.sh for $TARGET — do not edit."
  echo "set(SKIA_MILESTONE \"$SKIA_MILESTONE\")"
  echo "set(SKIA_COMMIT \"$SKIA_COMMIT\")"
  echo "set(SKIA_TARGET \"$TARGET\")"
  echo "set(SKIA_DEFINES"
  echo "$DEFINES" | sed 's/^/    "/; s/$/"/'
  echo ")"
  echo "set(SKIA_LIBRARIES"
  for l in "${LIBS[@]}"; do echo "    \"\${CMAKE_CURRENT_LIST_DIR}/lib/$l\""; done
  echo ")"
  echo "set(SKIA_SYSTEM_LIBRARIES"
  echo "$SYSLIBS" | sed '/^$/d; s/^/    "/; s/$/"/'
  echo ")"
} > "$OUT/SkiaConfig.cmake"

echo "==> installed to $OUT"
du -sh "$OUT/lib" | cut -f1
