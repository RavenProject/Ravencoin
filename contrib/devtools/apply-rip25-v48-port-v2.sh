#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo_root"

# Every conflict-heavy source transform is anchored to the exact audited 4.8
# input. Refuse to guess if the underlying baseline changed.
expected_configure_blob='7a657d5526f6fa57c5920a123622df54c77e5405'
actual_configure_blob="$(git hash-object configure.ac)"
if [[ "$actual_configure_blob" != "$expected_configure_blob" ]]; then
  echo "RIP-25/v4.8: configure.ac baseline mismatch: $actual_configure_blob" >&2
  exit 1
fi

validation_patch='contrib/devtools/patches/rip25-v48-validation-hardened.patch'
echo "[rip25-v48] checking $validation_patch"
git apply --check "$validation_patch"
echo "[rip25-v48] applying $validation_patch"
git apply "$validation_patch"

# liboqs is consensus-critical for this RIP-25 implementation. Build it as a
# mandatory dependency and require the ML-DSA-44 API provided by the pinned
# 0.12.0 baseline. This deliberately provides no --without-liboqs escape hatch.
python3 - <<'PY'
from pathlib import Path
p = Path('configure.ac')
s = p.read_text()

arg_anchor = '''AC_ARG_ENABLE([zmq],
  [AS_HELP_STRING([--disable-zmq],
  [disable ZMQ notifications])],
  [use_zmq=$enableval],
  [use_zmq=yes])
'''
arg_insert = arg_anchor + '''
AC_ARG_WITH([liboqs],
  [AS_HELP_STRING([--with-liboqs],
  [RIP-25 post-quantum signatures via liboqs >= 0.12.0 (required)])],
  [use_liboqs=$withval],
  [use_liboqs=yes])

AS_IF([test "x$use_liboqs" != "xyes"], [
  AC_MSG_ERROR([RIP-25 requires liboqs >= 0.12.0; --without-liboqs is not supported])
])
'''
if s.count(arg_anchor) != 1:
    raise SystemExit('configure.ac: unexpected ZMQ option anchor count')
s = s.replace(arg_anchor, arg_insert, 1)

lib_anchor = '''save_CXXFLAGS="${CXXFLAGS}"
'''
lib_insert = '''dnl RIP-25: consensus-critical ML-DSA-44 dependency.
if test x$use_pkgconfig = xyes; then
  PKG_CHECK_MODULES([LIBOQS], [liboqs >= 0.12.0], [],
    [AC_MSG_ERROR([RIP-25 requires liboqs >= 0.12.0])])
else
  AC_CHECK_HEADER([oqs/oqs.h], [],
    [AC_MSG_ERROR([RIP-25 requires liboqs >= 0.12.0 headers])])
  AC_CHECK_LIB([oqs], [OQS_SIG_new], [LIBOQS_LIBS=-loqs],
    [AC_MSG_ERROR([RIP-25 requires liboqs >= 0.12.0 library])])
  LIBOQS_CFLAGS=""
fi

AC_COMPILE_IFELSE([
  AC_LANG_PROGRAM([[#include <oqs/oqs.h>]],
                  [[const char* alg = OQS_SIG_alg_ml_dsa_44; (void)alg;]])
], [], [AC_MSG_ERROR([RIP-25 requires liboqs >= 0.12.0 with ML-DSA-44 support])])
AC_DEFINE([HAVE_LIBOQS], [1], [Define to 1 for required RIP-25 liboqs support])
AC_SUBST(LIBOQS_LIBS)
AC_SUBST(LIBOQS_CFLAGS)

''' + lib_anchor
if s.count(lib_anchor) != 1:
    raise SystemExit('configure.ac: unexpected CXXFLAGS anchor count')
s = s.replace(lib_anchor, lib_insert, 1)
p.write_text(s)
PY

# Materialized consensus/build invariants.
grep -Fq 'IsPQHybridActiveLocked' src/validation.cpp
grep -Fq 'VersionBitsStateSinceHeight' src/validation.cpp
grep -Fq 'IsPQWitnessV2Prevout' src/validation.cpp
grep -Fq 'GetContextualPQWitnessDiscount' src/validation.cpp
grep -Fq 'GetMaxBlockWeightForPrevLocked' src/validation.cpp
grep -Fq 'SCRIPT_VERIFY_PQ_HYBRID' src/validation.cpp
grep -Fq 'bad-blk-height' src/validation.cpp
grep -Fq 'RIP-25 requires liboqs >= 0.12.0' configure.ac
grep -Fq 'liboqs >= 0.12.0' configure.ac
grep -Fq 'OQS_SIG_alg_ml_dsa_44' configure.ac
grep -Fq 'AC_SUBST(LIBOQS_LIBS)' configure.ac
grep -Fq 'AC_SUBST(LIBOQS_CFLAGS)' configure.ac

echo '[rip25-v48] consensus + required liboqs 0.12 materialized successfully'
