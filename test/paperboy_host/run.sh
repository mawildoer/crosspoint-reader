#!/bin/sh
# Fault-injection checks for PhoneSync's SD article store; no ESP32 hardware required.
set -eu
ROOT="$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="$(mktemp -d)"
trap 'rm -rf "$BUILD_DIR"' EXIT HUP INT TERM
"${CXX:-c++}" -std=c++20 -I"$ROOT/test/paperboy_host/stubs" \
  -I"$ROOT/lib/PhoneSync/include" -I"$ROOT/lib/PhoneSync/src" \
  "$ROOT/test/paperboy_host/store_check.cpp" "$ROOT/lib/PhoneSync/src/ArticleStore.cpp" \
  -o "$BUILD_DIR/store_check"
"$BUILD_DIR/store_check"
