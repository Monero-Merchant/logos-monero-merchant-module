#!/usr/bin/env bash
set -euo pipefail
POS_ROOT=$(cd "$(dirname "$0")/.." && pwd)
[[ ${1:-android} == android ]] || { echo 'Basecamp uses delivery_module; only Android JNI artifacts are built here.' >&2; exit 2; }
mkdir -p "$POS_ROOT/native/artifacts"
for entry in 'arm64:arm64-v8a' 'x86_64:x86_64'; do
    target=${entry%%:*}
    abi=${entry##*:}
    nix build "path:$POS_ROOT/native/delivery#android-$target" --out-link "$POS_ROOT/native/artifacts/delivery-$abi" -L
done
