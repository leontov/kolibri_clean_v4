#!/usr/bin/env bash
set -euo pipefail
xcode-select --install || true
if ! command -v brew >/dev/null; then
  echo "Install Homebrew: https://brew.sh/"
  exit 1
fi
brew install openssl@3 pkg-config || true
export PKG_CONFIG_PATH="$(brew --prefix openssl@3)/lib/pkgconfig"
make -j"$(sysctl -n hw.ncpu)"
echo "Binaries in ./bin"
