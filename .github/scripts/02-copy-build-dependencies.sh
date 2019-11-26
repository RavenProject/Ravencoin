#!/usr/bin/env bash

OS=${1}
GITHUB_WORKSPACE=${2}

if [[ ! ${OS} || ! ${GITHUB_WORKSPACE} ]]; then
    echo "Error: Invalid options"
    echo "Usage: ${0} <operating system> <github workspace path>"
    exit 1
fi
echo "----------------------------------------"
echo "OS: ${OS}"
echo "----------------------------------------"

if [[ ${OS} == "arm32v7-disable-wallet" || ${OS} == "linux-disable-wallet" ]]; then
    OS=`echo ${OS} | cut -d"-" -f1`
fi

echo "----------------------------------------"
echo "Retrieving Dependencies for ${OS}"
echo "----------------------------------------"

cd ${GITHUB_WORKSPACE}
curl -O https://raven-build-resources.s3.amazonaws.com/${OS}/raven-${OS}-dependencies.tar.gz
curl -O https://raven-build-resources.s3.amazonaws.com/${OS}/SHASUM
if [[ $(sha256sum -c ${GITHUB_WORKSPACE}/SHASUM) ]]; then
    cd ${GITHUB_WORKSPACE}/depends
    tar zxvf ${GITHUB_WORKSPACE}/raven-${OS}-dependencies.tar.gz
else
    echo "SHASUM doesn't match"
    exit 1
fi