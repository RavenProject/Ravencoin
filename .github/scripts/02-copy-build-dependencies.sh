#!/usr/bin/env bash

OS=${1}
GITHUB_WORKSPACE=${2}
GITHUB_REF=${3}

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
    echo "OSX building is not currently enabled"
    exit 1
elif [[ ${OS} == "linux" || ${OS} == "linux-disable-wallet" ]]; then
    make HOST=x86_64-linux-gnu -j2
elif [[ ${OS} == "arm32v7" || ${OS} == "arm32v7-disable-wallet" ]]; then
    make HOST=arm-linux-gnueabihf -j2
elif [[ ${OS} == "aarch64" || ${OS} == "aarch64-disable-wallet" ]]; then
    make HOST=aarch64-linux-gnu -j2
fi
