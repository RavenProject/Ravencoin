#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo_root"

# Stage 1: audited consensus merge + mandatory liboqs 0.12 configure integration.
chmod +x contrib/devtools/apply-rip25-v48-port-v2.sh
./contrib/devtools/apply-rip25-v48-port-v2.sh

# Stage 2: miner must never assemble a block above the currently active
# consensus limit. The structural GetMaxBlockWeight() ceiling remains 16 MWU;
# this clamp enforces 8/12/16 contextually for block construction.
python3 - <<'PY'
from pathlib import Path
p = Path('src/miner.cpp')
s = p.read_text()
needle = 'nBlockMaxWeight = std::max((unsigned int)4000, std::min((unsigned int)GetMaxBlockWeight() - 4000, nBlockMaxWeight));'
if s.count(needle) != 1:
    raise SystemExit('miner.cpp: expected exactly one 4.8 block-weight clamp anchor')
replacement = needle + '''\n    nBlockMaxWeight = std::min(nBlockMaxWeight,\n        (unsigned int)GetMaxBlockWeightForPrev(chainActive.Tip(), chainparams.GetConsensus()) - 4000);'''
s = s.replace(needle, replacement, 1)
p.write_text(s)
PY

grep -Fq 'GetMaxBlockWeightForPrev(chainActive.Tip(), chainparams.GetConsensus())' src/miner.cpp

# Stage 3: preserve the already-approved RIP-25 init/wallet semantics by doing
# a strict 3-way merge from the exact pre-4.8 PR head. The current file is our
# 4.8 side, 6d48ae... is the original PR base, and 48e334... is the original
# RIP-25 side. Any conflict is fatal and must be resolved explicitly.
feature_base='6d48ae0175b10283248146ae3080e2ba70966739'
feature_head='48e334836536d66d4936dc1e5dbf548a0a17c0c3'

ensure_object() {
  local obj="$1"
  if ! git cat-file -e "$obj^{commit}" 2>/dev/null; then
    git fetch --no-tags --depth=1 origin "$obj"
  fi
}
ensure_object "$feature_base"
ensure_object "$feature_head"

merge_feature_file() {
  local path="$1"
  local tmp
  tmp="$(mktemp -d)"
  trap 'rm -rf "$tmp"' RETURN
  cp "$path" "$tmp/ours"
  git show "$feature_base:$path" > "$tmp/base"
  git show "$feature_head:$path" > "$tmp/theirs"
  if ! git merge-file -p "$tmp/ours" "$tmp/base" "$tmp/theirs" > "$tmp/merged"; then
    echo "RIP-25/v4.8: explicit merge required for $path" >&2
    exit 1
  fi
  if grep -Eq '^(<<<<<<<|=======|>>>>>>>)' "$tmp/merged"; then
    echo "RIP-25/v4.8: conflict marker in $path" >&2
    exit 1
  fi
  mv "$tmp/merged" "$path"
  rm -rf "$tmp"
  trap - RETURN
}

merge_feature_file src/init.cpp
merge_feature_file src/wallet/rpcwallet.cpp

# Port postconditions: capability + RPC are present, while 4.8 identity and
# overflow/exploit protections are still present elsewhere in the tree.
grep -Fq 'NODE_PQ_HYBRID' src/init.cpp
grep -Fq 'getnewpqaddress' src/wallet/rpcwallet.cpp
grep -Fq 'nHeightHeaderCheckActivation = 4487776' src/chainparams.cpp
grep -Fq 'DEPLOYMENT_TRANSFER_OVERFLOW' src/consensus/params.h
grep -Fq 'bad-blk-height' src/validation.cpp
grep -Fq 'define(_CLIENT_VERSION_MINOR, 8)' configure.ac
grep -Fq 'define(_CLIENT_VERSION_REVISION, 0)' configure.ac

echo '[rip25-v48] consensus + configure + miner + init + wallet materialized successfully'
