#!/usr/bin/env bash

set -euo pipefail

configuration="${1:-Release}"

if ! command -v cmake >/dev/null 2>&1; then
  echo "cmake is not installed or not available on PATH." >&2
  exit 1
fi

case "$configuration" in
  Debug)
    build_preset="build-linux-debug"
    ;;
  Release)
    build_preset="build-linux-release"
    ;;
  *)
    echo "Unsupported configuration: $configuration" >&2
    echo "Use Debug or Release." >&2
    exit 1
    ;;
esac

echo "Configuring with preset linux-default..."
cmake --preset linux-default

echo "Building with preset $build_preset..."
cmake --build --preset "$build_preset"

echo "Linux build completed successfully."
