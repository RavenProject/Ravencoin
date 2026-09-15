#!/bin/sh
# Copyright (c) 2014-2015 The Bitcoin Core developers
# Copyright (c) 2017-2019 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

set -eu

if [ -z "${OSSLSIGNCODE:-}" ]; then
  OSSLSIGNCODE=osslsigncode
fi

if [ ! -n "$1" ]; then
  echo "usage: $0 <osslcodesign args>"
  echo "example: $0 -key codesign.key"
  exit 1
fi

OUT=signature-win.tar.gz
SRCDIR=unsigned
WORKDIR=./.tmp
OUTDIR="${WORKDIR}/out"
OUTSUBDIR="${OUTDIR}/win"
TIMESERVER=http://timestamp.comodoca.com
CERTFILE="${WIN_CODESIGN_CERTFILE:-win-codesign.cert}"

if ! openssl x509 -checkend 0 -noout -in "${CERTFILE}" >/dev/null 2>&1; then
  echo "The Windows code-signing certificate is missing or expired." >&2
  exit 1
fi

mkdir -p "${OUTSUBDIR}"
found=0
for unsigned_path in "${SRCDIR}"/*-unsigned.exe; do
  [ -e "${unsigned_path}" ] || continue
  UNSIGNED=${unsigned_path##*/}
  found=1
  echo Signing "${UNSIGNED}"
  "${OSSLSIGNCODE}" sign -certs "${CERTFILE}" -t "${TIMESERVER}" -in "${unsigned_path}" -out "${WORKDIR}/${UNSIGNED}" "$@"
  "${OSSLSIGNCODE}" extract-signature -pem -in "${WORKDIR}/${UNSIGNED}" -out "${OUTSUBDIR}/${UNSIGNED}.pem"
  rm "${WORKDIR}/${UNSIGNED}"
done

if [ "${found}" -eq 0 ]; then
  echo "No unsigned Windows artifacts found in ${SRCDIR}." >&2
  exit 1
fi

rm -f "${OUT}"
tar -C "${OUTDIR}" -czf "${OUT}" .
rm -rf "${WORKDIR}"
echo "Created ${OUT}"
