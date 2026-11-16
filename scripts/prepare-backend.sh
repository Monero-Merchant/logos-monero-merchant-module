#!/usr/bin/env bash
set -euo pipefail
REPO_ROOT=$(cd "$(dirname "$0")/.." && pwd)
ARTIFACT_DIR="$REPO_ROOT/artifacts/backend"
mkdir -p "$ARTIFACT_DIR"
# Install Delivery and libp2p from the official Logos package repository.
nix build "$REPO_ROOT#lgx-portable" --out-link "$ARTIFACT_DIR/merchant-backend" -L
nix build "$REPO_ROOT/ui#lgx-portable" --out-link "$ARTIFACT_DIR/merchant-ui" -L
