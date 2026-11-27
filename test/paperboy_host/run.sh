#!/bin/sh
# Fault-injection checks for PhoneSync's SD article store; no ESP32 hardware required.
set -eu
ROOT="$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="$(mktemp -d)"
trap 'rm -rf "$BUILD_DIR"' EXIT HUP INT TERM
"${CXX:-c++}" -std=c++20 -I"$ROOT/test/paperboy_host/stubs" \
  -I"$ROOT/lib/LibraryIndex" -I"$ROOT/lib/PhoneSync/include" -I"$ROOT/lib/PhoneSync/src" -I"$ROOT/src/activities/library" \
  "$ROOT/test/paperboy_host/touch_check.cpp" "$ROOT/lib/LibraryIndex/LibraryTouchStore.cpp" "$ROOT/test/paperboy_host/firmware_stage_check.cpp" "$ROOT/lib/PhoneSync/src/FirmwareStage.cpp" "$ROOT/test/paperboy_host/store_check.cpp" "$ROOT/lib/PhoneSync/src/ArticleStore.cpp" \
  -o "$BUILD_DIR/store_check"
"$BUILD_DIR/store_check"
python3 "$ROOT/test/paperboy_host/receiver_check.py" "$ROOT" "$BUILD_DIR/receiver_check.cpp"
"${CXX:-c++}" -std=c++20 -I"$ROOT/test/paperboy_host/stubs" \
  -I"$ROOT/lib/LibraryIndex" -I"$ROOT/lib/PhoneSync/include" -I"$ROOT/lib/PhoneSync/src" \
  "$BUILD_DIR/receiver_check.cpp" "$ROOT/lib/PhoneSync/src/FirmwareStage.cpp" \
  "$ROOT/lib/PhoneSync/src/ArticleStore.cpp" "$ROOT/lib/LibraryIndex/LibraryTouchStore.cpp" \
  -o "$BUILD_DIR/receiver_check"
"$BUILD_DIR/receiver_check"
