#!/usr/bin/env bash

OS=${1}

if [[ ! ${OS} ]]; then
    echo "Error: Invalid options"
    echo "Usage: ${0} <operating system>"
    exit 1
fi

echo "----------------------------------------"
echo "Installing Build Packages for ${OS}"
echo "----------------------------------------"

apt-get update

if [[ ${OS} == "windows" ]]; then
    apt-get install -y \
    automake \
    autotools-dev \
    bsdmainutils \
    build-essential \
    curl \
    mingw-w64 \
    mingw-w64-x86-64-dev \
    git \
    libcurl4-openssl-dev \
    libssl-dev \
    libtool \
    osslsigncode \
    nsis \
    pkg-config \
    python3 \
    rename \
    zip \
    bison

    update-alternatives --set x86_64-w64-mingw32-g++ /usr/bin/x86_64-w64-mingw32-g++-posix 


elif [[ ${OS} == "osx" ]]; then
    apt -y install \
    autoconf \
    automake \
    awscli \
    bsdmainutils \
    ca-certificates \
    cmake \
    curl \
    fonts-tuffy \
    g++ \
    git \
    imagemagick \
    libbz2-dev \
    libcap-dev \
    librsvg2-bin \
    libtiff-tools \
    libtool \
    libz-dev \
    p7zip-full \
    pkg-config \
    python3 \
    python3-dev \
    python3-setuptools \
    s3curl \
    sleuthkit \
    bison \
    libtinfo5
    
elif [[ ${OS} == "linux" || ${OS} == "linux-disable-wallet" || ${OS} == "aarch64" || ${OS} == "aarch64-disable-wallet" ]]; then
    apt -y install \
    apt-file \
    autoconf \
    automake \
    autotools-dev \
    binutils-aarch64-linux-gnu \
    binutils \
    bsdmainutils \
    build-essential \
    ca-certificates \
    curl \
    g++-aarch64-linux-gnu \
    g++-8-aarch64-linux-gnu \
    g++-8-multilib \
    gcc-8-aarch64-linux-gnu \
    gcc-8-multilib \
    git \
    gnupg \
    libtool \
    nsis \
    pbuilder \
    pkg-config \
    python3 \
    rename \
    ubuntu-dev-tools \
    xkb-data \
    zip \
    bison



elif [[ ${OS} == "arm32v7" || ${OS} == "arm32v7-disable-wallet" ]]; then
    apt -y install \
    autoconf \
    automake \
    binutils-aarch64-linux-gnu \
    binutils-arm-linux-gnueabihf \
    binutils \
    bsdmainutils \
    ca-certificates \
    curl \
    g++-aarch64-linux-gnu \
    g++-8-aarch64-linux-gnu \
    gcc-8-aarch64-linux-gnu \
    g++-arm-linux-gnueabihf \
    g++-8-arm-linux-gnueabihf \
    gcc-8-arm-linux-gnueabihf \
    g++-8-multilib \
    gcc-8-multilib \
    git \
    libtool \
    pkg-config \
    python3 \
    bison
else
    echo "you must pass the OS to build for"
    exit 1
fi
    update-alternatives --install /usr/bin/python python /usr/bin/python2 1
    update-alternatives --install /usr/bin/python python /usr/bin/python3 2
