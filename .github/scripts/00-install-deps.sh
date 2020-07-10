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
apt-get install -y software-properties-common
add-apt-repository ppa:bitcoin/bitcoin
apt-get update

if [[ ${OS} == "windows" ]]; then
    apt-get install -y \
    automake \
    autotools-dev \
    bsdmainutils \
    build-essential \
    curl \
    g++-mingw-w64-x86-64 \
    git \
    libcurl4-openssl-dev \
    libssl-dev \
    libtool \
    osslsigncode \
    nsis \
    pkg-config \
    python \
    rename \
    zip

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
    g++-7-multilib \
    gcc-7-multilib \
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
    python \
    python-dev \
    python-setuptools \
    s3curl \
    sleuthkit
elif [[ ${OS} == "linux" || ${OS} == "linux-disable-wallet" ]]; then
    apt -y install \
    apt-file \
    autoconf \
    automake \
    autotools-dev \
    binutils-gold \
    bsdmainutils \
    build-essential \
    ca-certificates \
    curl \
    g++-7-multilib \
    gcc-7-multilib \
    git \
    gnupg \
    libtool \
    nsis \
    pbuilder \
    pkg-config \
    python \
    rename \
    ubuntu-dev-tools \
    xkb-data \
    zip
elif [[ ${OS} == "arm32v7" || ${OS} == "arm32v7-disable-wallet" ]]; then
    echo "removing existing azure repositories"
    apt-add-repository -r 'deb http://azure.archive.ubuntu.com/ubuntu xenial InRelease'
    apt-add-repository -r 'deb http://azure.archive.ubuntu.com/ubuntu xenial-updates InRelease'
    apt-add-repository -r 'deb http://azure.archive.ubuntu.com/ubuntu xenial-backports InRelease'
    
    echo "adding apt repository for arm packages"
    apt-add-repository 'deb http://us-west1.gce.archive.ubuntu.com/ubuntu/ xenial InRelease'
    apt-add-repository 'deb http://us-west1.gce.archive.ubuntu.com/ubuntu/ xenial-updates InRelease'
    apt-add-repository 'deb http://us-west1.gce.archive.ubuntu.com/ubuntu/ xenial main restricted'
    apt-add-repository 'deb http://us-west1.gce.archive.ubuntu.com/ubuntu/ xenial-updates main restricted'
    apt-get update
    
    apt -y install \
    autoconf \
    automake \
    binutils-aarch64-linux-gnu \
    binutils-arm-linux-gnueabihf \
    binutils-gold \
    bsdmainutils \
    ca-certificates \
    curl \
    g++-aarch64-linux-gnu \
    g++-7-aarch64-linux-gnu \
    gcc-7-aarch64-linux-gnu \
    g++-arm-linux-gnueabihf \
    g++-7-arm-linux-gnueabihf \
    gcc-7-arm-linux-gnueabihf \
    g++-7-multilib \
    gcc-7-multilib \
    git \
    libtool \
    pkg-config \
    python
else
    echo "you must pass the OS to build for"
    exit 1
fi
