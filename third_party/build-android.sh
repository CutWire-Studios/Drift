#!/usr/bin/env bash
# Cross-builds the native dependencies Drift links that have no Android package: FFmpeg, x264,
# dav1d, zstd, OpenSSL (libcrypto) and SoundTouch. Output goes to
# third_party/prebuilt/android/<abi>/{include,lib}, which is where the root CMakeLists looks.
#
# JUCE and the ONNX Runtime headers are NOT here: JUCE is a FetchContent source tree CMake builds
# in-tree, and the app never links ONNX Runtime at all — it dlopens one at startup, so only the
# headers matter and cmake/FetchOnnxRuntime.cmake downloads those.
#
# Most deps are static and PIC (the app is one .so QtLoader dlopens; a non-PIC archive linked
# into a .so fails with R_AARCH64_ADR_PREL_PG_HI21). OpenSSL is the exception: static libcrypto.a
# for signing, plus libcrypto_3.so/libssl_3.so listed in QT_ANDROID_EXTRA_LIBS for Qt Network TLS.
#
# Usage: third_party/build-android.sh [abi] [api]
#   abi  arm64-v8a (default), armeabi-v7a or x86_64
#   api  minimum API level, default 28 — must match minSdk in the root CMakeLists
set -euo pipefail

ABI="${1:-arm64-v8a}"
API="${2:-28}"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$HERE/src"
OUT="$HERE/prebuilt/android/$ABI"

# Pinned to the version the desktop build is verified against (system ffmpeg is n8.1.2).
# Note "n8.0" is not a tag — FFmpeg tags are n8.0.3, n8.1, n8.1.2, ...
FFMPEG_TAG="n8.1.2"
X264_TAG="stable"
ZSTD_TAG="v1.5.7"
OPENSSL_TAG="openssl-3.6.3"
DAV1D_TAG="1.5.4"
# Upstream tags this release "2.4.1"; there is no plain "2.4.0".
SOUNDTOUCH_TAG="2.4.1"

: "${ANDROID_NDK_ROOT:?set ANDROID_NDK_ROOT to the NDK version Qt for Android was built against}"

# TRIPLE names the clang driver; CONFIG_TRIPLE is what autoconf-style configure scripts are told,
# and on 32-bit ARM the two differ: the compiler is armv7a-linux-androideabi-clang, while x264's
# configure wants the canonical arm-linux-androideabi to resolve its CPU and OS.
#
# ARM_CFLAGS is the armeabi-v7a instruction-set contract. The NDK clang driver leaves armv7a on
# vfpv3-d16, but the NDK CMake toolchain the app itself is built with sets ANDROID_ARM_NEON on by
# default — so without this, FFmpeg and x264 are compiled for a weaker CPU than the code calling
# them, and every hand-written NEON path in libswscale/libswresample is compiled out.
#
# MESON_CPU_FAMILY/MESON_CPU are dav1d's half of the same contract: meson cross files name the
# host CPU themselves rather than deriving it from the compiler.
ARM_CFLAGS=""
case "$ABI" in
  arm64-v8a)   FF_ARCH=aarch64; TRIPLE=aarch64-linux-android;    OSSL_TARGET=android-arm64
               MESON_CPU_FAMILY=aarch64; MESON_CPU=aarch64 ;;
  armeabi-v7a) FF_ARCH=arm;     TRIPLE=armv7a-linux-androideabi; OSSL_TARGET=android-arm
               CONFIG_TRIPLE=arm-linux-androideabi
               MESON_CPU_FAMILY=arm; MESON_CPU=armv7a
               ARM_CFLAGS="-march=armv7-a -mfloat-abi=softfp -mfpu=neon" ;;
  x86_64)      FF_ARCH=x86_64;  TRIPLE=x86_64-linux-android;     OSSL_TARGET=android-x86_64
               MESON_CPU_FAMILY=x86_64; MESON_CPU=x86_64 ;;
  *) echo "unsupported ABI: $ABI (expected arm64-v8a, armeabi-v7a or x86_64)" >&2; exit 1 ;;
esac
: "${CONFIG_TRIPLE:=$TRIPLE}"

TC="$ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/linux-x86_64"
export CC="$TC/bin/${TRIPLE}${API}-clang"
export CXX="$TC/bin/${TRIPLE}${API}-clang++"
export AR="$TC/bin/llvm-ar"
export RANLIB="$TC/bin/llvm-ranlib"
export STRIP="$TC/bin/llvm-strip"
export NM="$TC/bin/llvm-nm"
export PATH="$TC/bin:$PATH"

[ -x "$CC" ] || { echo "no compiler at $CC — is ANDROID_NDK_ROOT right?" >&2; exit 1; }

mkdir -p "$SRC" "$OUT"
JOBS="$(nproc)"

clone() { # clone <url> <tag> <dir>
  [ -d "$SRC/$3" ] || git clone --depth 1 --branch "$2" "$1" "$SRC/$3"
}

echo "==> building for $ABI (API $API) into $OUT"

# --- x264 --------------------------------------------------------------------
# Exporter's default H.264 preset. It probes avcodec_find_encoder_by_name at runtime, so this is
# not strictly required — without it the codec list simply comes up short.
clone https://code.videolan.org/videolan/x264.git "$X264_TAG" x264
( cd "$SRC/x264" && make distclean >/dev/null 2>&1 || true
  ./configure --prefix="$OUT" --host="$CONFIG_TRIPLE" --enable-static --enable-pic \
      --disable-cli --disable-opencl --sysroot="$TC/sysroot" \
      --extra-cflags="$ARM_CFLAGS"
  make -j"$JOBS" && make install )

# --- dav1d -------------------------------------------------------------------
# The only software AV1 decoder in this build. FFmpeg's native "av1" decoder is a hwaccel shell:
# with no hwaccel attached, av1dec's get_format logs "Your platform doesn't support hardware
# accelerated AV1 decoding" and returns ENOSYS for every frame — avcodec_open2 still succeeds, so
# ClipReader gets a decoder that silently never produces a picture (a black preview, not an error).
# av1_mediacodec only covers devices that expose an AV1 MediaCodec, and only above ClipReader's
# resolution floor, so without dav1d a large share of AV1 clips do not decode at all.
# meson is dav1d's only build system; it needs a cross file because it does not read $CC alone.
clone https://code.videolan.org/videolan/dav1d.git "$DAV1D_TAG" dav1d
MESON_C_ARGS=""
for f in $ARM_CFLAGS; do MESON_C_ARGS="$MESON_C_ARGS'$f', "; done
cat > "$SRC/dav1d/cross-$ABI.txt" <<EOF
[binaries]
c = '$CC'
cpp = '$CXX'
ar = '$AR'
strip = '$STRIP'
pkg-config = 'pkg-config'

[built-in options]
c_args = [$MESON_C_ARGS]
c_link_args = [$MESON_C_ARGS]

[host_machine]
system = 'android'
cpu_family = '$MESON_CPU_FAMILY'
cpu = '$MESON_CPU'
endian = 'little'
EOF
rm -rf "$SRC/dav1d/build-$ABI"
meson setup "$SRC/dav1d/build-$ABI" "$SRC/dav1d" --cross-file "$SRC/dav1d/cross-$ABI.txt" \
  --prefix="$OUT" --libdir=lib --buildtype=release --default-library=static \
  -Db_staticpic=true -Denable_tools=false -Denable_tests=false
ninja -C "$SRC/dav1d/build-$ABI" install

# --- FFmpeg ------------------------------------------------------------------
# No --disable-postproc: libpostproc was removed in FFmpeg 8.0 and configure hard-errors on the
# option. avformat and avcodec are always built, so they have no --enable- switch either.
#
# -fvisibility=hidden is load-bearing, not tidiness. FFmpeg's aarch64 assembly reaches its constant
# tables with adrp/add (e.g. tx_float_neon.S -> ff_tx_tab_32_float). That addressing is only legal
# against a symbol the linker can resolve statically, and a *global* symbol in a shared object is
# preemptible, so linking these archives into libdrift.so fails with
#   relocation R_AARCH64_ADR_PREL_PG_HI21 cannot be used against symbol ...; recompile with -fPIC
# despite --enable-pic being set (the asm is already PIC — the symbols are the problem). Hidden
# visibility makes them non-preemptible and the relocation valid. Safe because FFmpeg is linked
# *into* the app's .so and its API is never re-exported.
# Decoders and encoders both: Exporter is compiled in, so the muxers and encoders have to exist for
# it to have anything to offer. MediaCodec gives ClipReader the h264/hevc/av1/vp9 decoders it asks
# for by name on large frames; --enable-jni is what it is built on, not an extra. Prebuilts made
# before these flags simply have no such decoder and ClipReader stays on software.
#
# --enable-libdav1d is what makes AV1 decode at all here; see the dav1d section above. FFmpeg picks
# libdav1d over the native av1 shell for AV1 streams, so nothing in the app has to ask for it.
clone https://git.ffmpeg.org/ffmpeg.git "$FFMPEG_TAG" ffmpeg
( cd "$SRC/ffmpeg" && make distclean >/dev/null 2>&1 || true
  PKG_CONFIG_LIBDIR="$OUT/lib/pkgconfig" ./configure \
    --prefix="$OUT" \
    --target-os=android --arch="$FF_ARCH" --enable-cross-compile \
    --sysroot="$TC/sysroot" \
    --cc="$CC" --cxx="$CXX" --ar="$AR" --nm="$NM" --ranlib="$RANLIB" --strip="$STRIP" \
    --enable-static --disable-shared --enable-pic \
    --enable-gpl --enable-version3 --enable-libx264 --enable-libdav1d \
    --extra-cflags="-I$OUT/include -fvisibility=hidden $ARM_CFLAGS" --extra-ldflags="-L$OUT/lib" \
    --disable-programs --disable-doc --disable-avdevice \
    --disable-vaapi --disable-vdpau --disable-v4l2-m2m --disable-cuda-llvm \
    --disable-vulkan --disable-libdrm --disable-xlib \
    --enable-jni --enable-mediacodec \
    --enable-decoder=h264_mediacodec --enable-decoder=hevc_mediacodec \
    --enable-decoder=av1_mediacodec --enable-decoder=vp9_mediacodec \
    --enable-avfilter --enable-swscale --enable-swresample
  make -j"$JOBS" && make install )

# --- zstd --------------------------------------------------------------------
# .driftpkg addons and .drift project bundles.
clone https://github.com/facebook/zstd.git "$ZSTD_TAG" zstd
cmake -S "$SRC/zstd/build/cmake" -B "$SRC/zstd/build-$ABI" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI="$ABI" -DANDROID_PLATFORM="android-$API" \
  -DCMAKE_INSTALL_PREFIX="$OUT" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DZSTD_BUILD_SHARED=OFF -DZSTD_BUILD_STATIC=ON -DZSTD_BUILD_PROGRAMS=OFF \
  -DZSTD_BUILD_TESTS=OFF -DZSTD_BUILD_CONTRIB=OFF -DZSTD_LEGACY_SUPPORT=OFF
cmake --build "$SRC/zstd/build-$ABI" --target install

# --- OpenSSL -----------------------------------------------------------------
# Static libcrypto.a: Ed25519 verification of .driftpkg signatures (AddonPackage.cpp).
# Shared libcrypto_3.so / libssl_3.so: Qt Network's qopensslbackend dlopens these at runtime. It
# builds the file name as "lib" + crypto/ssl + ANDROID_OPENSSL_SUFFIX, which defaults to _3, and
# Android rejects versioned sonames (libssl.so.3) — so _3 has to be the library's real name, not a
# copy of one named something else.
#
# OpenSSL is therefore told to build them that way, with the two hunks KDAB's android_openssl
# carries as ssl_3.patch: shlib_variant puts the _3 suffix in the file names and in the SONAME the
# linker records, and mkdef.pl keeps the symbol version nodes named OPENSSL_3.0.0 instead of
# inheriting the variant into them. Renaming a finished .so with patchelf is what this replaced,
# and it produced a library whose runtime string table disagreed with its section headers; the
# Android linker rejected it at launch with
#   dlopen failed: cannot find "io" from verneed[0] in DT_NEEDED list for libcrypto_3.so
# while readelf still showed the file as well-formed.
clone https://github.com/openssl/openssl.git "$OPENSSL_TAG" openssl
( cd "$SRC/openssl" && make clean >/dev/null 2>&1 || true
  # Idempotent: the clone is reused across runs, so reset before patching.
  git checkout -- Configurations/15-android.conf util/mkdef.pl
  patch -p0 <<'OPENSSL_SO_VARIANT_PATCH'
--- Configurations/15-android.conf
+++ Configurations/15-android.conf
@@ -192,6 +192,7 @@
         bin_lflags       => "-pie",
         enable           => [ ],
         shared_extension => ".so",
+        shlib_variant => "_3",
     },
     "android-arm" => {
         ################################################################
--- util/mkdef.pl
+++ util/mkdef.pl
@@ -258,14 +258,14 @@
             print <<"_____";
 }${prevversion_s};
 _____
-            $prevversion_s = " OPENSSL${SO_VARIANT}_$thisversion";
+            $prevversion_s = " OPENSSL_$thisversion";
             $thisversion = '';  # Trigger start of next section
         }
         unless ($thisversion) {
             $indent = 0;
             $thisversion = $_->version();
             $currversion_s = '';
-            $currversion_s = "OPENSSL${SO_VARIANT}_$thisversion "
+            $currversion_s = "OPENSSL_$thisversion "
                 if $thisversion ne '*';
             print <<"_____";
 ${currversion_s}{
OPENSSL_SO_VARIANT_PATCH
  ANDROID_NDK_ROOT="$ANDROID_NDK_ROOT" ./Configure "$OSSL_TARGET" \
      -D__ANDROID_API__="$API" --prefix="$OUT" --openssldir="$OUT/ssl" \
      shared no-tests no-apps no-engine
  make depend
  make -j"$JOBS" SHLIB_VERSION_NUMBER= build_libs
  cp libcrypto.a "$OUT/lib/"
  cp libcrypto_3.so libssl_3.so "$OUT/lib/"
  cp -r include/openssl "$OUT/include/" )

# --- SoundTouch --------------------------------------------------------------
# Pitch shifting behind the voice effects. Sources include <soundtouch/SoundTouch.h>, so the
# headers must land in include/soundtouch — which is where its own install puts them.
clone https://codeberg.org/soundtouch/soundtouch.git "$SOUNDTOUCH_TAG" soundtouch
cmake -S "$SRC/soundtouch" -B "$SRC/soundtouch/build-$ABI" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI="$ABI" -DANDROID_PLATFORM="android-$API" \
  -DCMAKE_INSTALL_PREFIX="$OUT" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DBUILD_SHARED_LIBS=OFF -DSOUNDSTRETCH=OFF -DSOUNDTOUCH_DLL=OFF
cmake --build "$SRC/soundtouch/build-$ABI" --target install

echo
echo "==> done. $OUT/lib:"
ls -1 "$OUT/lib"/*.a "$OUT/lib"/libcrypto_3.so "$OUT/lib"/libssl_3.so 2>/dev/null || ls -1 "$OUT/lib"
