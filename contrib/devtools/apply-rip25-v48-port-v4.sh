#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo_root"
patch_dir="contrib/devtools/patches"

apply_exact() {
  local path="$1" pre="$2" post="$3" patch="$4"
  local current
  current="$(git hash-object "$path")"
  if [[ "$current" == "$post" ]]; then
    echo "[rip25-v48] already materialized: $path"
    return 0
  fi
  if [[ "$current" != "$pre" ]]; then
    echo "[rip25-v48] unexpected preimage for $path: $current (expected $pre)" >&2
    exit 1
  fi
  git apply --check "$patch_dir/$patch"
  git apply "$patch_dir/$patch"
  current="$(git hash-object "$path")"
  if [[ "$current" != "$post" ]]; then
    echo "[rip25-v48] postimage mismatch for $path: $current (expected $post)" >&2
    exit 1
  fi
  echo "[rip25-v48] materialized: $path -> $post"
}

# Consensus first. The postimage preserves the original Ravencoin 4.8
# bad-blk-height code while adding reorg-safe BIP9 activation, 8/12/16 MWU,
# and the approved 8x discount bound to real witness-v2 prevouts.
apply_exact src/validation.cpp \
  5f6db7d536d12f1d23e426c8d42287c7a70d759c \
  7ad7ff00aa1753fbc030754357b4404a192f4cb6 \
  rip25-v48-final-validation.patch

# liboqs is consensus-critical. Refuse builds that disable it or provide <0.12.
# The same postimage replaces the blunt -static in the Windows DLL archive_cmds
# with -Bstatic around the libtool libraries plus -static-libgcc/-static-libstdc++
# and a trailing -Bdynamic, so the -lpthread that the mingw-w64 posix driver adds
# from its own spec resolves against the winpthread import library that
# libravenconsensus already links, instead of pulling a second, static copy in.
apply_exact configure.ac \
  7a657d5526f6fa57c5920a123622df54c77e5405 \
  b525e9f056decdc4c6b55bfb38b7132e3e050bc4 \
  rip25-v48-final-configure.patch

# Miner must construct only blocks valid under the currently active phase.
apply_exact src/miner.cpp \
  d50501f287e269735abec979eb63167b559a2255 \
  2f8a9a509befbb674b541aaf475c4c9409e29459 \
  rip25-v48-final-miner.patch

# Advertise binary capability without changing BIP9 consensus activation.
apply_exact src/init.cpp \
  b3d5ea58568d771bdc9ded808a2bd2118a65ee99 \
  1ec012e076abacf0a02978bd55fe30e02220d026 \
  rip25-v48-final-init.patch

# Wallet policy needs an active-chain query and must not create unprotected
# witness-v2 addresses before RIP-25 is ACTIVE.
apply_exact src/validation.h \
  68bad0a088a443641dbfa6c516d81ee073ab50fd \
  979ec5da5b0708fcae0b1ef444a7264536def9de \
  rip25-v48-final-validation-h.patch
apply_exact src/wallet/rpcwallet.cpp \
  a1997356c100c6b66a9999ab2244f579d2a7d5a9 \
  dcb5bc3e496cbec53c79c63dfc25e2c8d0d3fea7 \
  rip25-v48-final-rpcwallet.patch

# 4.8 owns bit 11 for TRANSFER_OVERFLOW, therefore RIP-25 documentation must
# describe the ported deployment on bit 12. Preserve the approved 8x and
# 8 -> 12 -> 16 MWU design unchanged.
python3 - <<'PY'
from pathlib import Path
p = Path('doc/RIP-0025-PQ-Signatures.md')
s = p.read_text()
old = '  bit:                                    11'
new = '  bit:                                    12'
if old in s:
    if s.count(old) != 1:
        raise SystemExit('RIP-25 spec: unexpected bit-11 occurrence count')
    s = s.replace(old, new, 1)
elif new not in s:
    raise SystemExit('RIP-25 spec: neither expected bit 11 nor bit 12 deployment line found')
p.write_text(s)
PY

# libravenconsensus is a shared DLL on MinGW. AX_PTHREAD selects -pthread as
# PTHREAD_CFLAGS on the CI toolchain; AM_LDFLAGS would therefore make GCC add
# libpthread.a when the custom archive_cmds_CXX later forces -static. The PIC
# objects need __imp_pthread_* from libwinpthread.dll.a instead. On Windows,
# remove the pthread driver flag from this DLL's link and use only the import
# archive. Keep the normal PTHREAD_CFLAGS/PTHREAD_LIBS behavior elsewhere.
python3 - <<'PY'
from pathlib import Path
p = Path('src/Makefile.am')
s = p.read_text()

ld_old = 'libravenconsensus_la_LDFLAGS = $(AM_LDFLAGS) -no-undefined $(RELDFLAGS)'
ld_new = '''if TARGET_WINDOWS
libravenconsensus_la_LDFLAGS = $(LIBTOOL_LDFLAGS) $(HARDENED_LDFLAGS) -no-undefined $(RELDFLAGS)
else
libravenconsensus_la_LDFLAGS = $(AM_LDFLAGS) -no-undefined $(RELDFLAGS)
endif'''
if ld_old in s:
    s = s.replace(ld_old, ld_new, 1)
elif ld_new not in s:
    raise SystemExit('Makefile.am: expected libravenconsensus LDFLAGS line not found')

base_old = 'libravenconsensus_la_LIBADD = $(LIBSECP256K1) $(BOOST_LIBS) $(LIBOQS_LIBS) $(PTHREAD_LIBS)'
base_new = 'libravenconsensus_la_LIBADD = $(LIBSECP256K1) $(BOOST_LIBS) $(LIBOQS_LIBS)'
if base_old in s:
    s = s.replace(base_old, base_new, 1)
elif base_new not in s:
    raise SystemExit('Makefile.am: expected libravenconsensus LIBADD base line not found')

win_old = 'libravenconsensus_la_LIBADD += -Wl,-Bdynamic -lwinpthread -Wl,-Bstatic'
win_new = 'libravenconsensus_la_LIBADD += /usr/$(host)/lib/libwinpthread.dll.a\nelse\nlibravenconsensus_la_LIBADD += $(PTHREAD_LIBS)'
if win_old in s:
    s = s.replace(win_old, win_new, 1)
elif win_new not in s:
    raise SystemExit('Makefile.am: expected winpthread LIBADD block not found')

comment_old = '''# The windows DLL archive_cmds (configure.ac) forces -static into the link
# line so libgcc/libstdc++/libssp are linked statically. That -static also
# forces ld to resolve subsequent -l lookups against plain .a archives only,
# so a bare -lwinpthread would bind against libwinpthread.a, which does not
# export the __imp_-prefixed symbols that older mingw-w64 headers (as shipped
# on the CI runner) require when pthread.h is compiled with DLL_EXPORT defined
# (as libtool does for PIC/shared objects). Bracket the winpthread lookup in
# -Bdynamic/-Bstatic so it resolves against libwinpthread.dll.a (the import
# library) instead, then restore -Bstatic for anything linked after it.'''
comment_new = '''# AX_PTHREAD selected -pthread for this MinGW toolchain. The target-specific
# LDFLAGS above intentionally omit PTHREAD_CFLAGS here because -pthread plus
# archive_cmds_CXX -static would inject libpthread.a. The PIC objects instead
# need the DLL import symbols (__imp_pthread_*), so link only the import archive.
# Non-Windows targets retain the normal AM_LDFLAGS and PTHREAD_LIBS paths.'''
if comment_old in s:
    s = s.replace(comment_old, comment_new, 1)
elif comment_new not in s:
    raise SystemExit('Makefile.am: expected winpthread explanatory comment not found')

p.write_text(s)
PY

git diff --check
grep -Fq '  bit:                                    12' doc/RIP-0025-PQ-Signatures.md
grep -Fq 'PQ_WITNESS_SCALE_FACTOR = 8' src/consensus/consensus.h
grep -Fq 'MAX_BLOCK_WEIGHT_RIP25_PHASE1 = 12000000' src/consensus/consensus.h
grep -Fq 'MAX_BLOCK_WEIGHT_RIP25_PHASE2 = 16000000' src/consensus/consensus.h
grep -Fq 'libravenconsensus_la_LDFLAGS = $(LIBTOOL_LDFLAGS) $(HARDENED_LDFLAGS) -no-undefined $(RELDFLAGS)' src/Makefile.am
grep -Fq 'libravenconsensus_la_LDFLAGS = $(AM_LDFLAGS) -no-undefined $(RELDFLAGS)' src/Makefile.am
grep -Fq 'libravenconsensus_la_LIBADD = $(LIBSECP256K1) $(BOOST_LIBS) $(LIBOQS_LIBS)' src/Makefile.am
grep -Fq 'libravenconsensus_la_LIBADD += /usr/$(host)/lib/libwinpthread.dll.a' src/Makefile.am
grep -Fq 'libravenconsensus_la_LIBADD += $(PTHREAD_LIBS)' src/Makefile.am
grep -Fq '${wl}-Bstatic \$deplibs \$compiler_flags -static-libgcc -static-libstdc++ ${wl}-Bdynamic' configure.ac
if grep -Fq '\$libobjs \$deplibs \$compiler_flags -static -o' configure.ac; then
  echo '[rip25-v48] stale blunt -static remains in the Windows DLL archive_cmds' >&2
  exit 1
fi
if grep -Fq 'libravenconsensus_la_LIBADD = $(LIBSECP256K1) $(BOOST_LIBS) $(LIBOQS_LIBS) $(PTHREAD_LIBS)' src/Makefile.am; then
  echo '[rip25-v48] stale unconditional PTHREAD_LIBS remains on libravenconsensus' >&2
  exit 1
fi
if grep -Fq 'libravenconsensus_la_LIBADD += -Wl,-Bdynamic -lwinpthread -Wl,-Bstatic' src/Makefile.am; then
  echo '[rip25-v48] stale Automake-invalid winpthread LIBADD line remains' >&2
  exit 1
fi

echo '[rip25-v48] final audited port materialized successfully'
