#!/usr/bin/env bash
# Build Snes9x on macOS: the Qt front end, the native Cocoa app and the libretro core.
#
#   ./build-mac.sh            # all three
#   ./build-mac.sh qt         # snes9x-qt   -> build/qt/snes9x-qt  (needs: brew install qt pkgconf libpng ninja)
#   ./build-mac.sh app        # Snes9x.app  -> build/xcode/Build/Products/Release/Snes9x.app
#   ./build-mac.sh libretro   # core        -> libretro/snes9x_libretro.dylib
#   ./build-mac.sh test       # build + run the corruption engine tests (tests/)
#   ./build-mac.sh asan       # the same tests under AddressSanitizer (build/tests-asan)
#
# Notes for current Xcode:
#  - The project pins MACOSX_DEPLOYMENT_TARGET=10.12, which Xcode 26+ rejects; we override to 12.0.
#  - The bundle is re-signed ad hoc after the build so it launches locally.
#  - shaders.metal needs the Metal Toolchain component; it is downloaded on first run if missing.
set -euo pipefail
cd "$(dirname "$0")"

target="${1:-all}"

submodules() {
  git submodule update --init --recursive \
    external/SPIRV-Cross external/glslang external/vulkan-headers external/cubeb
}

build_app() {
  submodules
  if ! xcrun -f metal >/dev/null 2>&1 || ! xcrun metal --version >/dev/null 2>&1; then
    xcodebuild -downloadComponent MetalToolchain
  fi
  xcodebuild -project macosx/snes9x.xcodeproj -scheme snes9x -configuration Release \
    -derivedDataPath build/xcode \
    CODE_SIGN_IDENTITY=- CODE_SIGNING_REQUIRED=NO ONLY_ACTIVE_ARCH=YES \
    MACOSX_DEPLOYMENT_TARGET=12.0 \
    build | tail -2
  # The app and its embedded framework get different team IDs, so dyld refuses to load it; re-sign as one.
  codesign --force --deep -s - build/xcode/Build/Products/Release/Snes9x.app
  echo "App: $(pwd)/build/xcode/Build/Products/Release/Snes9x.app"
}

build_qt() {
  submodules
  cmake -S qt -B build/qt -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix qt)" >/dev/null
  cmake --build build/qt | tail -1
  echo "Qt: $(pwd)/build/qt/snes9x-qt"
}

build_test() {
  local qt_prefix=""
  if command -v brew >/dev/null && brew --prefix qt >/dev/null 2>&1; then qt_prefix="$(brew --prefix qt)"; fi
  cmake -S tests -B build/tests -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="$qt_prefix" >/dev/null
  if ! out=$(cmake --build build/tests 2>&1); then echo "$out" | grep -v '^\[' >&2; exit 1; fi
  build/tests/corrupt-tests "$@"
  if [ -x build/tests/corrupt-headless-tests ] && [ $# -eq 0 ]; then
    # The core prints ROM info on every load; keep just the test results.
    build/tests/corrupt-headless-tests | grep -E "ERROR|FATAL|values|test cases|Status"
  fi
  if [ -x build/tests/corrupt-qt-tests ] && [ $# -eq 0 ]; then
    # Offscreen; PNGs of the dialogs land in build/tests/shots.
    CORRUPT_SHOTS_DIR=build/tests/shots build/tests/corrupt-qt-tests 2>&1 | grep -vE "^qt\\.qpa|propagateSizeHints" | grep -E "ERROR|FATAL|CHECK|values|test cases|Status|error"
  fi
}

build_asan() {
  local qt_prefix=""
  if command -v brew >/dev/null && brew --prefix qt >/dev/null 2>&1; then qt_prefix="$(brew --prefix qt)"; fi
  cmake -S tests -B build/tests-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="$qt_prefix" \
    -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address" >/dev/null
  if ! out=$(cmake --build build/tests-asan 2>&1); then echo "$out" | grep -v '^\[' >&2; exit 1; fi
  build/tests-asan/corrupt-tests | grep -E "AddressSanitizer|test cases|Status"
  [ -x build/tests-asan/corrupt-headless-tests ] && build/tests-asan/corrupt-headless-tests | grep -E "AddressSanitizer|test cases|Status"
  [ -x build/tests-asan/corrupt-qt-tests ] && QT_QPA_PLATFORM=offscreen build/tests-asan/corrupt-qt-tests 2>&1 | grep -E "AddressSanitizer|test cases|Status"
  return 0
}

build_libretro() {
  make -C libretro -j"$(sysctl -n hw.ncpu)" >/dev/null
  echo "Core: $(pwd)/libretro/snes9x_libretro.dylib"
}

case "$target" in
  qt) build_qt ;;
  test) shift; build_test "$@" ;;
  asan) build_asan ;;
  app) build_app ;;
  libretro) build_libretro ;;
  all) build_qt; build_app; build_libretro ;;
  *) echo "usage: $0 [qt|app|libretro|test|asan|all]" >&2; exit 2 ;;
esac
