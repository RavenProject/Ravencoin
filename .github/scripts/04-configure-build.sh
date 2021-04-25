#!/usr/bin/env bash

OS=${1}
GITHUB_WORKSPACE=${2}

if [[ ! ${OS} || ! ${GITHUB_WORKSPACE} ]]; then
    echo "Error: Invalid options"
    echo "Usage: ${0} <operating system> <github workspace path> <disable wallet (true | false)>"
    exit 1
fi

if [[ ${OS} == "windows" ]]; then
    CONFIG_SITE=${GITHUB_WORKSPACE}/depends/x86_64-w64-mingw32/share/config.site ./configure --prefix=/ --disable-ccache --disable-maintainer-mode --disable-dependency-tracking --enable-reduce-exports --disable-bench --disable-tests --disable-gui-tests --enable-shared=no CFLAGS="-O2 -g" CXXFLAGS="-O2 -g"
elif [[ ${OS} == "osx" ]]; then
    CONFIG_SITE=${GITHUB_WORKSPACE}/depends/x86_64-apple-darwin14/share/config.site ./configure --prefix=/ --disable-ccache --disable-maintainer-mode --disable-dependency-tracking --enable-reduce-exports --disable-bench --disable-gui-tests GENISOIMAGE=${GITHUB_WORKSPACE}/depends/x86_64-apple-darwin14/native/bin/genisoimage
elif [[ ${OS} == "linux" || ${OS} == "linux-disable-wallet" ]]; then
    if [[ ${OS} == "linux-disable-wallet" ]]; then
        EXTRA_OPTS="--disable-wallet"
    fi
    CONFIG_SITE=${GITHUB_WORKSPACE}/depends/x86_64-linux-gnu/share/config.site ./configure --prefix=/ --disable-ccache --disable-maintainer-mode --disable-dependency-tracking --enable-glibc-back-compat --enable-reduce-exports --disable-bench --disable-gui-tests CFLAGS="-O2 -g" CXXFLAGS="-O2 -g" LDFLAGS="-static-libstdc++" ${EXTRA_OPTS}
elif [[ ${OS} == "arm32v7" || ${OS} == "arm32v7-disable-wallet" ]]; then
    if [[ ${OS} == "arm32v7-disable-wallet" ]]; then
        EXTRA_OPTS="--disable-wallet"
        CONFIG_SITE=${GITHUB_WORKSPACE}/depends/arm-linux-gnueabihf/share/config.site ./configure --prefix=/ --enable-glibc-back-compat --enable-reduce-exports LDFLAGS=-static-libstdc++ --disable-tests --with-libs=no --with-gui=no ${EXTRA_OPTS}
    else
        CONFIG_SITE=${GITHUB_WORKSPACE}/depends/arm-linux-gnueabihf/share/config.site ./configure --prefix=/ --disable-ccache --disable-maintainer-mode --disable-dependency-tracking --enable-glibc-back-compat --enable-reduce-exports --disable-bench --disable-gui-tests CFLAGS="-O2 -g" CXXFLAGS="-O2 -g" LDFLAGS="-static-libstdc++"
    fi
elif [[ ${OS} == "aarch64" || ${OS} == "aarch64-disable-wallet" ]]; then
    if [[ ${OS} == "aarch64-disable-wallet" ]]; then
        EXTRA_OPTS="--disable-wallet"
        CONFIG_SITE=${GITHUB_WORKSPACE}/depends/aarch64-linux-gnu/share/config.site ./configure --prefix=/ --enable-glibc-back-compat --enable-reduce-exports LDFLAGS=-static-libstdc++ --disable-tests --with-libs=no --with-gui=no ${EXTRA_OPTS}
    else
        CONFIG_SITE=${GITHUB_WORKSPACE}/depends/aarch64-linux-gnu/share/config.site ./configure --prefix=/ --disable-ccache --disable-maintainer-mode --disable-dependency-tracking --enable-glibc-back-compat --enable-reduce-exports --disable-bench --disable-gui-tests CFLAGS="-O2 -g" CXXFLAGS="-O2 -g" LDFLAGS="-static-libstdc++"
    fi
else
    echo "You must pass an OS."
    echo "Usage: ${0} <operating system> <github workspace path> <disable wallet (true | false)>"
    exit 1
fi
