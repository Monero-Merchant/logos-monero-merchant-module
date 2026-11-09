#!/usr/bin/env bash
set -euo pipefail
REPO_ROOT=$(cd "$(dirname "$0")/.." && pwd)
ARTIFACT_DIR="$REPO_ROOT/artifacts/backend"
MODULE='github:logos-co/logos-delivery-module/586e1a383ab2286be0c3a5f1d0f864a8fd37f5b8'
mkdir -p "$ARTIFACT_DIR"
nix build "$MODULE#lgx-portable" --out-link "$ARTIFACT_DIR/delivery-module" -L
SYSTEM=$(nix eval --impure --raw --expr builtins.currentSystem)
nix build --impure --expr "(builtins.getFlake \"$MODULE\").inputs.libp2p_module.packages.$SYSTEM.lgx-portable" \
    --out-link "$ARTIFACT_DIR/discovery-module" -L
nix build "$REPO_ROOT#lgx-portable" --out-link "$ARTIFACT_DIR/merchant-backend" -L
nix build "$REPO_ROOT/ui#lgx-portable" --out-link "$ARTIFACT_DIR/merchant-ui" -L
