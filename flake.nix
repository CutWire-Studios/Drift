{
  description = "Drift: beginner-friendly open-source video editor (Qt 6, QML, FFmpeg)";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs =
    { self, nixpkgs }:
    let
      systems = [
        "x86_64-linux"
        "aarch64-linux"
      ];
      eachSystem = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});

      # Dependencies the packaged build and the dev shell share. Each is its own derivation, so
      # Nix rebuilds only Drift after an edit and reuses Skia and JUCE from the store.
      mkDeps =
        pkgs:
        let
          lib = pkgs.lib;
        in
        {
          # Audio effect DSP only: Drift compiles juce_dsp and its dependencies into driftaudiodsp
          # from this source tree, so configure downloads nothing.
          juce = pkgs.fetchFromGitHub {
            owner = "juce-framework";
            repo = "JUCE";
            tag = "8.0.15";
            hash = "sha256-41pi/JrkHquT7+SQ9H8pnAenaO1alDdqWl/UKMPM9Aw=";
          };

          # Headers only: Drift compiles against ONNX Runtime and never links it. The user installs
          # the runtime (CPU, CUDA, WebGPU) as an addon. Same pinned release as
          # cmake/FetchOnnxRuntime.cmake; the GitHub release archives are gone, so this is
          # Microsoft's package-registry copy (an .aar, which is a zip).
          onnxruntimeHeaders = pkgs.stdenv.mkDerivation {
            pname = "onnxruntime-headers";
            version = "1.27.0";
            src = pkgs.fetchurl {
              url = "https://repo1.maven.org/maven2/com/microsoft/onnxruntime/onnxruntime-android/1.27.0/onnxruntime-android-1.27.0.aar";
              sha256 = "077dec5e2d821234c7dc0aba584bec8f999854b546c754cab93a90741c56fbeb";
            };
            nativeBuildInputs = [ pkgs.unzip ];
            unpackPhase = "unzip -q $src";
            installPhase = ''
              mkdir -p $out/include
              cp -a headers/. $out/include/
            '';
          };

          # The repository's own third_party/build-skia.sh builds Skia, so the GN argument list
          # lives in one place. Only that script is an input here, so Nix does not rebuild Skia
          # when Drift changes.
          skia = pkgs.stdenv.mkDerivation {
            pname = "skia-drift";
            version = "m148-e7c90ec";
            # Keep url and hash in step with SKIA_COMMIT/SKIA_SHA256 in build-skia.sh.
            src = pkgs.fetchurl {
              url = "https://github.com/google/skia/archive/e7c90ecca9444fe09598f1630ab7cee2c0ee027a.tar.gz";
              sha256 = "7b5158952ebc4e2ce8de19f036b7613e266705003469579d9e98070ead62fc3b";
            };
            nativeBuildInputs = with pkgs; [
              gn
              ninja
              python3
              pkg-config
            ];
            # The gn package's setup hook runs its own `gn gen` in configurePhase and would bypass
            # the argument list in build-skia.sh.
            dontUseGnConfigure = true;
            # is_official_build=true points Skia at the system copies of these.
            buildInputs = with pkgs; [
              fontconfig
              freetype
              harfbuzz
              icu
              expat
              libpng
              zlib
              libGL
              libx11
              xorgproto
            ];
            buildPhase = ''
              runHook preBuild
              export SKIA_SRC_DIR="$PWD"
              export SKIA_BUILD_DIR="$TMPDIR/skia-build"
              export SKIA_OUT_DIR="$TMPDIR/skia"
              # Skia's GN files hardcode /usr/include/harfbuzz and /usr/include/freetype2 for the
              # system copies; CPATH puts the real store paths after those (absent) entries.
              export CPATH="${lib.getDev pkgs.harfbuzz}/include/harfbuzz:${lib.getDev pkgs.freetype}/include/freetype2:''$CPATH"
              SKIA_SYSTEM_ICU=1 \
              CC=${lib.getExe pkgs.clang} \
              CXX=${lib.getExe' pkgs.clang "clang++"} \
              JOBS=$NIX_BUILD_CORES \
                bash ${./third_party/build-skia.sh} ${if pkgs.stdenv.hostPlatform.isAarch64 then "linux-arm64" else "linux-x64"}
              runHook postBuild
            '';
            installPhase = ''
              mkdir -p $out
              cp -a "$SKIA_OUT_DIR"/. $out/
            '';
            meta.platforms = lib.platforms.linux;
          };
        };

      mkDrift =
        pkgs:
        let
          lib = pkgs.lib;
          deps = mkDeps pkgs;
        in
        pkgs.stdenv.mkDerivation {
          pname = "drift";
          version = "0.9.4";

          src = lib.cleanSourceWith {
            src = ./.;
            filter =
              path: type:
              lib.cleanSourceFilter path type
              && !(type == "directory" && lib.hasPrefix "build" (baseNameOf path));
          };

          nativeBuildInputs = with pkgs; [
            cmake
            ninja
            pkg-config
            qt6.wrapQtAppsHook
          ];

          buildInputs = with pkgs; [
            qt6.qtbase
            qt6.qtdeclarative
            qt6.qtmultimedia
            qt6.qtsvg
            qt6.qtimageformats
            qt6.qtwayland
            qt6.qttools
            ffmpeg_7
            zstd
            openssl
            soundtouch
            zlib
            deps.skia
            deps.juce
            deps.onnxruntimeHeaders
            # Skia::Skia links these by bare name (GN resolved them as system libraries).
            harfbuzz
            freetype
            fontconfig
            icu
            expat
            libpng
            libGL
            libx11
          ];

          cmakeFlags = [
            (lib.cmakeFeature "CMAKE_BUILD_TYPE" "Release")
            # CMake finds Skia through its generated SkiaConfig.cmake, which carries the build's
            # defines and link order.
            (lib.cmakeBool "DRIFT_WITH_SKIA" true)
            (lib.cmakeFeature "DRIFT_SKIA_DIR" "${deps.skia}")
            (lib.cmakeBool "DRIFT_FETCH_ONNXRUNTIME" false)
            (lib.cmakeBool "DRIFT_BUNDLE_ONNXRUNTIME" false)
            (lib.cmakeFeature "ONNXRUNTIME_ROOT" "${deps.onnxruntimeHeaders}")
            (lib.cmakeBool "DRIFT_FETCH_JUCE" false)
            (lib.cmakeFeature "DRIFT_JUCE_DIR" "${deps.juce}")
            # A packaged build ships the core effect set; the rest come from the Addon Manager.
            (lib.cmakeBool "DRIFT_BUNDLE_CORE_PACKAGES_ONLY" true)
            # "source" is the marketplace's name for a plain build; the enum has no nix value.
            (lib.cmakeFeature "DRIFT_DISTRIBUTION" "source")
            # Nix owns updates, and the build ships the committed catalogs compiled.
            (lib.cmakeFeature "DRIFT_UPDATE_FEED_URL" "")
            (lib.cmakeBool "DRIFT_AUTO_UPDATE_TRANSLATIONS" false)
            # Tests need a real OpenGL context and an X server; the sandbox has neither.
            (lib.cmakeBool "DRIFT_BUILD_TESTS" false)
          ];

          # wrapQtAppsHook wires Qt's plugin and QML import paths from the buildInputs above.
          meta = {
            description = "Beginner-friendly open-source video editor that uses Qt 6, QML and FFmpeg";
            homepage = "https://cutwire.org/drift";
            license = lib.licenses.gpl3Plus;
            mainProgram = "drift";
            platforms = lib.platforms.linux;
            sourceProvenance = [ lib.sourceTypes.fromSource ];
          };
        };
    in
    {
      packages = eachSystem (pkgs: {
        drift = mkDrift pkgs;
        default = self.packages.${pkgs.stdenv.hostPlatform.system}.drift;
      });

      # `nix develop` gives the working tree a build environment; docs/BUILDING.md has the cmake
      # invocation. Same dependency set the packaged build uses, plus the directories CMake would
      # otherwise have to download.
      devShells = eachSystem (
        pkgs:
        let
          deps = mkDeps pkgs;
        in
        {
          default = pkgs.mkShell {
            inputsFrom = [ self.packages.${pkgs.stdenv.hostPlatform.system}.drift ];
            packages = [ pkgs.clang-tools ];
            shellHook = ''
              export DRIFT_SKIA_DIR="${deps.skia}"
              export DRIFT_JUCE_DIR="${deps.juce}"
              export ONNXRUNTIME_ROOT="${deps.onnxruntimeHeaders}"
            '';
          };
        }
      );
    };
}
