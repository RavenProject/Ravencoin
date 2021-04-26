Build instructions for Ravencoin 
=================================
Ubuntu 21.04 - Hirsute Hippo
---------------------------------
This will install most of the dependencies from ubuntu.
The only one we build, is Berkeley DB 4.8.


Install dependencies:
----------------------------
`$ sudo apt install build-essential
libssl-dev
libboost-chrono1.71-dev
libboost-filesystem1.71-dev
libboost-program-options1.71-dev
libboost-system1.71-dev
libboost-thread1.71-dev
libboost-test1.71-dev
qtbase5-dev
qttools5-dev
bison
libexpat1-dev
libdbus-1-dev
libfontconfig-dev
libfreetype-dev
libice-dev
libsm-dev
libx11-dev
libxau-dev
libxext-dev
libevent-dev
libxcb1-dev
libxkbcommon-dev
libminiupnpc-dev
libprotobuf-dev
libqrencode-dev
xcb-proto
x11proto-xext-dev
x11proto-dev
xtrans-dev
zlib1g-dev
libczmq-dev
autoconf
automake
libtool
protobuf-compiler
`

Directory structure
------------------
Ravencoin sources in `$HOME/src`

Berkeley DB will be installed to `$HOME/src/db4`


Ravencoin
------------------

Start in $HOME

Make the directory for sources and go into it.

`mkdir src`

`cd src`

__Download Ravencoin source.__

`git clone https://github.com/RavenProject/Ravencoin`

`cd Ravencoin`

`git checkout develop` # this checks out the develop branch.

__Download and build Berkeley DB 4.8__

`contrib/install_db4.sh ../`

_This requires [PR#934](https://github.com/RavenProject/Ravencoin/pull/934) to be merged into develop._

__The build process:__

`./autogen.sh`

`export BDB_PREFIX='$HOME/src/db4'`

`./configure BDB_LIBS="-L${BDB_PREFIX}/lib -ldb_cxx-4.8" BDB_CFLAGS="-I${BDB_PREFIX}/include" --prefix=/usr/local` 

_Adjust to own needs. This will install the binaries to `/usr/local/bin`_


`make -j8`  # 8 for 8 build threads, adjust to fit your setup.

You can now start raven-qt from the build directory.

`src/qt/raven-qt`

ravend and raven-cli are in `src/`


__Optional:__

`sudo make install`  # if you want to install the binaries to /usr/local/bin.





