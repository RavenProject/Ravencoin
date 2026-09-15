#!/usr/bin/env bash
set -euo pipefail

OS=${1:-}
GITHUB_WORKSPACE=${2:-}
GITHUB_REF=${3:-}

MACOS_SDK_FILE="Xcode-11.3.1-11C505-extracted-SDK-with-libcxx-headers.tar.gz"
MACOS_SDK_URL="https://bitcoincore.org/depends-sources/sdks/${MACOS_SDK_FILE}"
MACOS_SDK_SHA256="436df6dfc7073365d12f8ef6c1fdb060777c720602cc67c2dcf9a59d94290e38"

if [[ ! ${OS} || ! ${GITHUB_WORKSPACE} ]]; then
    echo "Error: Invalid options"
    echo "Usage: ${0} <operating system> <github workspace path>"
    exit 1
fi
echo "----------------------------------------"
echo "OS: ${OS}"
echo "----------------------------------------"

if [[ ${OS} == "arm32v7-disable-wallet" || ${OS} == "linux-disable-wallet" || ${OS} == "aarch64-disable-wallet" ]]; then
    OS=`echo ${OS} | cut -d"-" -f1`
fi

echo "----------------------------------------"
echo "Building Dependencies for ${OS}"
echo "----------------------------------------"

cd depends
if [[ ${OS} == "windows" ]]; then
    make HOST=x86_64-w64-mingw32 -j2
elif [[ ${OS} == "osx" ]]; then
    mkdir SDKs
    cd SDKs
    curl --location --fail --silent --show-error --connect-timeout 20 --retry 3 --retry-delay 2 \
        --output "${MACOS_SDK_FILE}" "${MACOS_SDK_URL}"
    printf '%s  %s\n' "${MACOS_SDK_SHA256}" "${MACOS_SDK_FILE}" | sha256sum -c -
    tar -zxf "${MACOS_SDK_FILE}"
    rm -f "${MACOS_SDK_FILE}"
    cd ..
    make HOST=x86_64-apple-darwin14 -j2
elif [[ ${OS} == "linux" || ${OS} == "linux-disable-wallet" ]]; then
    make HOST=x86_64-linux-gnu -j2
elif [[ ${OS} == "arm32v7" || ${OS} == "arm32v7-disable-wallet" ]]; then
    make HOST=arm-linux-gnueabihf -j2
elif [[ ${OS} == "aarch64" || ${OS} == "aarch64-disable-wallet" ]]; then
    make HOST=aarch64-linux-gnu -j2
fi