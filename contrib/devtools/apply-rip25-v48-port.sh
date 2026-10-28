#!/usr/bin/env bash
set -euo pipefail

# Deterministic integration-only application of the audited RIP-25-on-4.8 port.
# The patches are kept explicit and reviewable; upstream PR #1281 must receive
# flattened source only after the fork build gate is 5/5 green.
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo_root"

patches=(
  contrib/devtools/patches/rip25-v48-validation-hardened.patch
)

for patch in "${patches[@]}"; do
  echo "[rip25-v48] checking $patch"
  git apply --check "$patch"
done

for patch in "${patches[@]}"; do
  echo "[rip25-v48] applying $patch"
  git apply "$patch"
done

# Consensus-critical invariants that must be present in the materialized tree.
grep -Fq 'IsPQHybridActiveLocked' src/validation.cpp
grep -Fq 'VersionBitsStateSinceHeight' src/validation.cpp
grep -Fq 'IsPQWitnessV2Prevout' src/validation.cpp
grep -Fq 'GetContextualPQWitnessDiscount' src/validation.cpp
grep -Fq 'GetMaxBlockWeightForPrevLocked' src/validation.cpp
grep -Fq 'SCRIPT_VERIFY_PQ_HYBRID' src/validation.cpp
grep -Fq 'bad-blk-height' src/validation.cpp

echo '[rip25-v48] audited port materialized successfully'
