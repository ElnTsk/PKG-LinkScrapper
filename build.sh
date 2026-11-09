#!/usr/bin/env bash
# cross-build the windows binary from linux (zig bundles clang, mingw headers and crt)
set -euo pipefail
cd "$(dirname "$0")"

ZIG="${ZIG:-$HOME/tools/zig-x86_64-linux-0.17.1/zig}"
TARGET="${TARGET:-x86_64-windows-gnu}"
COMMON=(-std=c++20 -O2 -Wall -Wextra -Wno-unused-parameter)
CORE=(src/util.cpp src/json.cpp src/hosts.cpp src/html.cpp src/match.cpp src/config.cpp src/http.cpp
      src/crawl.cpp src/verify.cpp src/parts.cpp src/trust.cpp src/ping.cpp src/output.cpp
      src/deadstore.cpp src/download.cpp src/resolve.cpp src/catalog.cpp src/report.cpp)
GUI_LIBS=(-lwinhttp -lcomctl32 -lcomdlg32 -lshell32 -lole32 -luuid -lgdi32 -luser32 -ldwmapi
          -luxtheme -ladvapi32 -lws2_32)

[ -x "$ZIG" ] || { echo "zig not found at $ZIG"; exit 1; }

mkdir -p build

echo "== selftest (native linux)"
"$ZIG" c++ "${COMMON[@]}" -target x86_64-linux-gnu \
  test/selftest.cpp src/util.cpp src/json.cpp src/hosts.cpp src/html.cpp src/match.cpp src/parts.cpp \
  src/catalog.cpp \
  -o build/selftest
./build/selftest

echo "== manifest resource"
(cd res && "$ZIG" rc app.rc ../build/app.res)

echo "== PKG-LinkScrapper.exe (one file: windowed, themed, defaults compiled in)"
"$ZIG" c++ "${COMMON[@]}" -target "$TARGET" -static \
  "${CORE[@]}" src/gui.cpp build/app.res \
  -Wl,--subsystem,windows \
  "${GUI_LIBS[@]}" -o build/PKG-LinkScrapper.exe

# console build, for pipes and scheduled jobs - not part of the single-file deliverable
echo "== PKG-LinkScrapper-cli.exe (console)"
"$ZIG" c++ "${COMMON[@]}" -target "$TARGET" -static \
  -lwinhttp -lws2_32 -lole32 -ladvapi32 \
  "${CORE[@]}" src/main.cpp -o build/PKG-LinkScrapper-cli.exe

python3 - <<'PY'
import struct
for name in ("build/PKG-LinkScrapper.exe", "build/PKG-LinkScrapper-cli.exe"):
    data = open(name, "rb").read(4096)
    off = struct.unpack_from("<I", data, 0x3C)[0]
    subsystem = struct.unpack_from("<H", data, off + 24 + 68)[0]
    has_rsrc = b".rsrc" in data
    print(f"{name}: subsystem {subsystem} ({'gui' if subsystem == 2 else 'console'}), resource section {has_rsrc}")
PY

ls -la build/PKG-LinkScrapper.exe build/PKG-LinkScrapper-cli.exe
