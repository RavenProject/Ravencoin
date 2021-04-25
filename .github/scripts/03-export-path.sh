#!/usr/bin/env bash

OS=${1}
GITHUB_WORKSPACE=${2}

if [[ ! ${OS} || ! ${GITHUB_WORKSPACE} ]]; then
    echo "Error: Invalid options"
    echo "Usage: ${0} <operating system> <github workspace path>"
    exit 1
fi

if [[ ${OS} == "windows" ]]; then
    export PATH=${GITHUB_WORKSPACE}/depends/x86_64-w64-mingw32/native/bin:${PATH}
elif [[ ${OS} == "osx" ]]; then
    export PATH=${GITHUB_WORKSPACE}/depends/x86_64-apple-darwin14/native/bin:${PATH}
elif [[ ${OS} == "linux" || ${OS} == "linux-disable-wallet" ]]; then
    export PATH=${GITHUB_WORKSPACE}/depends/x86_64-linux-gnu/native/bin:${PATH}
elif [[ ${OS} == "arm32v7" || ${OS} == "arm32v7-disable-wallet" ]]; then
    export PATH=${GITHUB_WORKSPACE}/depends/arm-linux-gnueabihf/native/bin:${PATH}
elif [[ ${OS} == "aarch64" || ${OS} == "aarch64-disable-wallet" ]]; then
    export PATH=${GITHUB_WORKSPACE}/depends/aarch64-linux-gnu/native/bin:${PATH}
else
    echo "You must pass an OS."
    echo "Usage: ${0} <operating system> <github workspace path>"
    exit 1
fi
