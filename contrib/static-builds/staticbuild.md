# Static linux 64 builds #
From an ubuntu 18 bionic server(required)
```
cd ~/
export PATH_orig=$PATH
DISTNAME=raven-2.0.1
sudo apt install -y curl g++-aarch64-linux-gnu g++-7-aarch64-linux-gnu gcc-7-aarch64-linux-gnu binutils-aarch64-linux-gnu g++-arm-linux-gnueabihf g++-7-arm-linux-gnueabihf gcc-7-arm-linux-gnueabihf binutils-arm-linux-gnueabihf g++-7-multilib gcc-7-multilib binutils-gold git pkg-config autoconf libtool automake bsdmainutils ca-certificates python
git clone https://github.com/ravenproject/ravencoin
mkdir -p release
cd ravencoin/depends
make HOST=x86_64-linux-gnu -j4
cd ~/ravencoin
export PATH=$PWD/depends/x86_64-linux-gnu/native/bin:$PATH
sudo ./autogen.sh
CONFIG_SITE=$PWD/depends/x86_64-linux-gnu/share/config.site ./configure --prefix=/ --disable-ccache --disable-maintainer-mode --disable-dependency-tracking --enable-glibc-back-compat --enable-reduce-exports --disable-bench --disable-gui-tests CFLAGS="-O2 -g" CXXFLAGS="-O2 -g" LDFLAGS="-static-libstdc++"
make -j4 
make -C src check-security
make -C src check-symbols 
mkdir ~/linux64
make install DESTDIR=~/linux64/$DISTNAME
cd ~/linux64
sudo find . -name "lib*.la" -delete
sudo find . -name "lib*.a" -delete
sudo rm -rf $DISTNAME/lib/pkgconfig
sudo find ${DISTNAME}/bin -type f -executable -exec ../ravencoin/contrib/devtools/split-debug.sh {} {} {}.dbg \;
sudo find ${DISTNAME}/lib -type f -exec ../ravencoin/contrib/devtools/split-debug.sh {} {} {}.dbg \;
find $DISTNAME/ -not -name "*.dbg" | sort | tar --no-recursion --mode='u+rw,go+r-w,a+X' --owner=0 --group=0 -c -T - | gzip -9n > ~/release/$DISTNAME-x86_64-linux-gnu.tar.gz
cd ~/ravencoin
sudo rm -rf ~/linux64
make clean
export PATH=$PATH_orig
```
## Build general sourcecode while we are at it ##
```
export PATH_orig=$PATH
cd ~/ravencoin
export PATH=$PWD/depends/x86_64-linux-gnu/native/bin:$PATH
sudo ./autogen.sh
CONFIG_SITE=$PWD/depends/x86_64-linux-gnu/share/config.site ./configure --prefix=/
make dist
SOURCEDIST=`echo raven-*.tar.gz`
mkdir -p temp
cd temp
tar xf ../$SOURCEDIST
find raven-* | sort | tar --no-recursion --mode='u+rw,go+r-w,a+X' --owner=0 --group=0 -c -T - | gzip -9n > ../$SOURCEDIST
cd ~/ravencoin
mv $SOURCEDIST ~/release
sudo rm -rf temp
make clean
export PATH=$PATH_orig
```



# Static linux 32 builds #
From an ubuntu 18 bionic server(required)
```
cd ~/
export PATH_orig=$PATH
DISTNAME=raven-2.0.1
sudo apt install -y curl g++-aarch64-linux-gnu g++-7-aarch64-linux-gnu gcc-7-aarch64-linux-gnu binutils-aarch64-linux-gnu g++-arm-linux-gnueabihf g++-7-arm-linux-gnueabihf gcc-7-arm-linux-gnueabihf binutils-arm-linux-gnueabihf g++-7-multilib gcc-7-multilib binutils-gold git pkg-config autoconf libtool automake bsdmainutils ca-certificates python
git clone https://github.com/ravenproject/ravencoin
mkdir -p release
mkdir -p ~/wrapped
mkdir -p ~/wrapped/extra_includes
mkdir -p ~/wrapped/extra_includes/i686-pc-linux-gnu
sudo ln -s /usr/include/x86_64-linux-gnu/asm ~/wrapped/extra_includes/i686-pc-linux-gnu/asm

#the following can be copy/pasted as a single block of code

for prog in gcc g++; do
rm -f ~/wrapped/${prog}
cat << EOF > ~/wrapped/${prog}
#!/usr/bin/env bash
REAL="`which -a ${prog} | grep -v ~/wrapped/${prog} | head -1`"
for var in "\$@"
do
  if [ "\$var" = "-m32" ]; then
    export C_INCLUDE_PATH="$PWD/wrapped/extra_includes/i686-pc-linux-gnu"
    export CPLUS_INCLUDE_PATH="$PWD/wrapped/extra_includes/i686-pc-linux-gnu"
    break
  fi
done
\$REAL \$@
EOF
chmod +x ~/wrapped/${prog}
done

#end of single block of code

export PATH=~/wrapped:$PATH
export HOST_ID_SALT="~/wrapped/extra_includes/i386-linux-gnu"
cd ravencoin/depends
make HOST=i686-pc-linux-gnu -j4
unset HOST_ID_SALT
cd ~/ravencoin
export PATH=$PWD/depends/i686-pc-linux-gnu/native/bin:$PATH
sudo ./autogen.sh
CONFIG_SITE=$PWD/depends/i686-pc-linux-gnu/share/config.site ./configure --prefix=/ --disable-ccache --disable-maintainer-mode --disable-dependency-tracking --enable-glibc-back-compat --enable-reduce-exports --disable-bench --disable-gui-tests CFLAGS="-O2 -g" CXXFLAGS="-O2 -g" LDFLAGS="-static-libstdc++"
make -j4 
make -C src check-security 
mkdir ~/linux32
make install DESTDIR=~/linux32/$DISTNAME
cd ~/linux32
sudo find . -name "lib*.la" -delete
sudo find . -name "lib*.a" -delete
sudo rm -rf $DISTNAME/lib/pkgconfig
sudo find ${DISTNAME}/bin -type f -executable -exec ../ravencoin/contrib/devtools/split-debug.sh {} {} {}.dbg \;
sudo find ${DISTNAME}/lib -type f -exec ../ravencoin/contrib/devtools/split-debug.sh {} {} {}.dbg \;
find $DISTNAME/ -not -name "*.dbg" | sort | tar --no-recursion --mode='u+rw,go+r-w,a+X' --owner=0 --group=0 -c -T - | gzip -9n > ~/release/$DISTNAME-i686-pc-linux-gnu.tar.gz
cd ~/ravencoin
sudo rm -rf ~/linux32
sudo rm -rf ~/wrapped
make clean
export PATH=$PATH_orig
```



# Static linux ARM builds #
From an ubuntu 18 bionic server(required)
```
cd ~/
export PATH_orig=$PATH
DISTNAME=raven-2.0.1
sudo apt install -y curl g++-aarch64-linux-gnu g++-7-aarch64-linux-gnu gcc-7-aarch64-linux-gnu binutils-aarch64-linux-gnu g++-arm-linux-gnueabihf g++-7-arm-linux-gnueabihf gcc-7-arm-linux-gnueabihf binutils-arm-linux-gnueabihf g++-7-multilib gcc-7-multilib binutils-gold git pkg-config autoconf libtool automake bsdmainutils ca-certificates python
git clone https://github.com/ravenproject/ravencoin
mkdir -p release
cd ravencoin/depends
make HOST=arm-linux-gnueabihf -j4
cd ~/ravencoin
export PATH=$PWD/depends/arm-linux-gnueabihf/native/bin:$PATH
sudo ./autogen.sh
CONFIG_SITE=$PWD/depends/arm-linux-gnueabihf/share/config.site ./configure --prefix=/ --disable-ccache --disable-maintainer-mode --disable-dependency-tracking --enable-glibc-back-compat --enable-reduce-exports --disable-bench --disable-gui-tests CFLAGS="-O2 -g" CXXFLAGS="-O2 -g" LDFLAGS="-static-libstdc++"
make -j4 
make -C src check-security
mkdir ~/linuxARM
make install DESTDIR=~/linuxARM/$DISTNAME
cd ~/linuxARM
sudo find . -name "lib*.la" -delete
sudo find . -name "lib*.a" -delete
sudo rm -rf $DISTNAME/lib/pkgconfig
sudo find ${DISTNAME}/bin -type f -executable -exec ../ravencoin/contrib/devtools/split-debug.sh {} {} {}.dbg \;
sudo find ${DISTNAME}/lib -type f -exec ../ravencoin/contrib/devtools/split-debug.sh {} {} {}.dbg \;
find $DISTNAME/ -not -name "*.dbg" | sort | tar --no-recursion --mode='u+rw,go+r-w,a+X' --owner=0 --group=0 -c -T - | gzip -9n > ~/release/$DISTNAME-arm-linux-gnueabihf.tar.gz
cd ~/ravencoin
sudo rm -rf ~/linuxARM
make clean
export PATH=$PATH_orig
```



# Static linux aarch64 builds #
From an ubuntu 18 bionic server(required)
```
cd ~/
export PATH_orig=$PATH
DISTNAME=raven-2.0.1
sudo apt install -y curl g++-aarch64-linux-gnu g++-7-aarch64-linux-gnu gcc-7-aarch64-linux-gnu binutils-aarch64-linux-gnu g++-arm-linux-gnueabihf g++-7-arm-linux-gnueabihf gcc-7-arm-linux-gnueabihf binutils-arm-linux-gnueabihf g++-7-multilib gcc-7-multilib binutils-gold git pkg-config autoconf libtool automake bsdmainutils ca-certificates python
git clone https://github.com/ravenproject/ravencoin
mkdir -p release
cd ravencoin/depends
make HOST=aarch64-linux-gnu -j4
cd ~/ravencoin
export PATH=$PWD/depends/aarch64-linux-gnu/native/bin:$PATH
sudo ./autogen.sh
CONFIG_SITE=$PWD/depends/aarch64-linux-gnu/share/config.site ./configure --prefix=/ --disable-ccache --disable-maintainer-mode --disable-dependency-tracking --enable-glibc-back-compat --enable-reduce-exports --disable-bench --disable-gui-tests CFLAGS="-O2 -g" CXXFLAGS="-O2 -g" LDFLAGS="-static-libstdc++"
make -j4 
make -C src check-security
mkdir ~/linuxaarch64
make install DESTDIR=~/linuxaarch64/$DISTNAME
cd ~/linuxaarch64
sudo find . -name "lib*.la" -delete
sudo find . -name "lib*.a" -delete
sudo rm -rf $DISTNAME/lib/pkgconfig
sudo find ${DISTNAME}/bin -type f -executable -exec ../ravencoin/contrib/devtools/split-debug.sh {} {} {}.dbg \;
sudo find ${DISTNAME}/lib -type f -exec ../ravencoin/contrib/devtools/split-debug.sh {} {} {}.dbg \;
find $DISTNAME/ -not -name "*.dbg" | sort | tar --no-recursion --mode='u+rw,go+r-w,a+X' --owner=0 --group=0 -c -T - | gzip -9n > ~/release/$DISTNAME-aarch64-linux-gnu.tar.gz
cd ~/ravencoin
sudo rm -rf ~/linuxaarch64
make clean
export PATH=$PATH_orig
```



# Static windows 64 builds #
From an ubuntu 18 bionic server(required)
```
cd ~/
export PATH_orig=$PATH
DISTNAME=raven-2.0.1
sudo apt install -y build-essential libtool autotools-dev automake pkg-config bsdmainutils curl git python nsis rename zip
sudo apt install -y g++-mingw-w64-x86-64
sudo update-alternatives --config x86_64-w64-mingw32-g++ # Set the default mingw32 g++ compiler option to posix.
git clone https://github.com/ravenproject/ravencoin
mkdir -p release
mkdir -p release/unsigned/
mkdir -p sign/win64
PATH=$(echo "$PATH" | sed -e 's/:\/mnt.*//g') # strip out problematic Windows %PATH% imported var
cd ravencoin/depends
make HOST=x86_64-w64-mingw32 -j4
cd ~/ravencoin
export PATH=$PWD/depends/x86_64-w64-mingw32/native/bin:$PATH
sudo ./autogen.sh
CONFIG_SITE=$PWD/depends/x86_64-w64-mingw32/share/config.site ./configure --prefix=/ --disable-ccache --disable-maintainer-mode --disable-dependency-tracking --enable-reduce-exports --disable-bench --disable-gui-tests CFLAGS="-O2 -g" CXXFLAGS="-O2 -g"
make -j4 
make -C src check-security
make deploy
rename 's/-setup\.exe$/-setup-unsigned.exe/' *-setup.exe
cp -f raven-*setup*.exe ~/release/unsigned/
mkdir -p ~/win64
sudo make install DESTDIR=~/win64/$DISTNAME
cd ~/win64
sudo mv ~/win64/$DISTNAME/bin/*.dll ~/win64/$DISTNAME/lib/
sudo find . -name "lib*.la" -delete
sudo find . -name "lib*.a" -delete
sudo rm -rf $DISTNAME/lib/pkgconfig
sudo find $DISTNAME/bin -type f -executable -exec x86_64-w64-mingw32-objcopy --only-keep-debug {} {}.dbg \; -exec x86_64-w64-mingw32-strip -s {} \; -exec x86_64-w64-mingw32-objcopy --add-gnu-debuglink={}.dbg {} \;
find ./$DISTNAME -not -name "*.dbg"  -type f | sort | zip -X@ ./$DISTNAME-x86_64-w64-mingw32.zip
mv ./$DISTNAME-x86_64-*.zip ~/release/$DISTNAME-win64.zip
cd ~/
sudo rm -rf win64
cp -rf ravencoin/contrib/windeploy ~/sign/win64
cd ~/sign/win64/windeploy
mkdir unsigned
mv ~/ravencoin/raven-*setup-unsigned.exe unsigned/
find . | sort | tar --no-recursion --mode='u+rw,go+r-w,a+X' --owner=0 --group=0 -c -T - | gzip -9n > ~/sign/$DISTNAME-win64-unsigned.tar.gz
cd ~/sign
sudo rm -rf win64
cd ~/ravencoin
sudo rm -rf release
make clean
export PATH=$PATH_orig
#transfer ~/sign/raven-*-win*-unsigned.tar.gz to the signing machine
```



# Signing windows 64 binaries #
From an Ubuntu 16.04 xenial machine !important (openssl 1.0.2 required)  
This process requires core to have a pvk file (kept secret)and a cert in PEM format(from comodo) as a part of the repo at contrib/windeploy
```
cd ~/
DISTNAME=raven-2.0.1
sudo apt install build-essential libtool autotools-dev automake pkg-config bsdmainutils libcurl4-openssl-dev curl libssl-dev autoconf
wget https://bitcoincore.org/cfields/osslsigncode-Backports-to-1.7.1.patch
wget http://downloads.sourceforge.net/project/osslsigncode/osslsigncode/osslsigncode-1.7.1.tar.gz
tar xf osslsigncode-1.7.1.tar.gz
cd osslsigncode-1.7.1
patch -p1 < ../osslsigncode-Backports-to-1.7.1.patch
./configure --without-gsf  --disable-dependency-tracking
make
sudo make install
cd ~/
mkdir win64 && cd win64/
#transfer raven-*-win*-unsigned.tar.gz to ~/win64/  
tar xf $DISTNAME-win*-unsigned.tar.gz
rm $DISTNAME-win*-unsigned.tar.gz
./detached-sig-create.sh -key /path/to/codesign.pvk
#Enter the passphrase for the key when prompted
tar xf signature-win.tar.gz 
osslsigncode attach-signature -in "unsigned/$DISTNAME-win64-setup-unsigned.exe" -out "$DISTNAME-win64-setup.exe" -sigin "win/$DISTNAME-win64-setup-unsigned.exe.pem"
#transfer raven-*-win*-setup.exe back to the Ubuntu18 build machine to the folder ~/release (to shasum with the rest of the releases)
```



# Static windows 32 builds #
From an ubuntu 18 bionic server(required)
```
cd ~/
export PATH_orig=$PATH
DISTNAME=raven-2.0.1
sudo apt install -y build-essential libtool autotools-dev automake pkg-config bsdmainutils curl git python nsis rename zip
sudo apt install -y g++-mingw-w64-i686 mingw-w64-i686-dev
sudo update-alternatives --config i686-w64-mingw32-g++  # Set the default mingw32 g++ compiler option to posix.
git clone https://github.com/ravenproject/ravencoin
mkdir -p release
mkdir -p release/unsigned/
mkdir -p sign/win32
PATH=$(echo "$PATH" | sed -e 's/:\/mnt.*//g') # strip out problematic Windows %PATH% imported var
cd ravencoin/depends
make HOST=i686-w64-mingw32 -j4
cd ~/ravencoin
export PATH=$PWD/depends/i686-w64-mingw32/native/bin:$PATH
sudo ./autogen.sh
CONFIG_SITE=$PWD/depends/i686-w64-mingw32/share/config.site ./configure --prefix=/ --disable-ccache --disable-maintainer-mode --disable-dependency-tracking --enable-reduce-exports --disable-bench --disable-gui-tests CFLAGS="-O2 -g" CXXFLAGS="-O2 -g"
make -j4 
make -C src check-security
make deploy
rename 's/-setup\.exe$/-setup-unsigned.exe/' *-setup.exe
cp -f raven-*setup*.exe ~/release/unsigned/
mkdir -p ~/win32
sudo make install DESTDIR=~/win32/$DISTNAME
cd ~/win32
sudo mv ~/win32/$DISTNAME/bin/*.dll ~/win32/$DISTNAME/lib/
sudo find . -name "lib*.la" -delete
sudo find . -name "lib*.a" -delete
sudo rm -rf $DISTNAME/lib/pkgconfig
sudo find $DISTNAME/bin -type f -executable -exec i686-w64-mingw32-objcopy --only-keep-debug {} {}.dbg \; -exec i686-w64-mingw32-strip -s {} \; -exec i686-w64-mingw32-objcopy --add-gnu-debuglink={}.dbg {} \;
find ./$DISTNAME -not -name "*.dbg"  -type f | sort | zip -X@ ./$DISTNAME-i686-w64-mingw32.zip
mv ./$DISTNAME-i686-w64-*.zip ~/release/$DISTNAME-win32.zip
cd ~/
sudo rm -rf win32
cp -rf ravencoin/contrib/windeploy ~/sign/win32
cd ~/sign/win32/windeploy
mkdir unsigned
mv ~/ravencoin/raven-*setup-unsigned.exe unsigned/
find . | sort | tar --no-recursion --mode='u+rw,go+r-w,a+X' --owner=0 --group=0 -c -T - | gzip -9n > ~/sign/$DISTNAME-win32-unsigned.tar.gz
cd ~/sign
sudo rm -rf win32
cd ~/ravencoin
sudo rm -rf release
make clean
export PATH=$PATH_orig
#transfer ~/sign/raven-*-win*-unsigned.tar.gz to the signing machine
```



# Signing windows 32 binaries #
From an Ubuntu 16.04 xenial machine !important (openssl 1.0.2 required)  
This process requires core to have a pvk file (kept secret)and a cert in PEM format(from comodo) as a part of the repo at contrib/windeploy
```
cd ~/
DISTNAME=raven-2.0.1
sudo apt install build-essential libtool autotools-dev automake pkg-config bsdmainutils libcurl4-openssl-dev curl libssl-dev autoconf
wget https://bitcoincore.org/cfields/osslsigncode-Backports-to-1.7.1.patch
wget http://downloads.sourceforge.net/project/osslsigncode/osslsigncode/osslsigncode-1.7.1.tar.gz
tar xf osslsigncode-1.7.1.tar.gz
cd osslsigncode-1.7.1
patch -p1 < ../osslsigncode-Backports-to-1.7.1.patch
./configure --without-gsf  --disable-dependency-tracking
make
sudo make install
cd ~/
mkdir win32 && cd win32/
#transfer raven-*-win*-unsigned.tar.gz to ~/win32/  
tar xf $DISTNAME-win*-unsigned.tar.gz
rm $DISTNAME-win*-unsigned.tar.gz
./detached-sig-create.sh -key /path/to/codesign.pvk
#Enter the passphrase for the key when prompted
tar xf signature-win.tar.gz 
osslsigncode attach-signature -in "unsigned/$DISTNAME-win32-setup-unsigned.exe" -out "$DISTNAME-win32-setup.exe" -sigin "win/$DISTNAME-win32-setup-unsigned.exe.pem"
# Transfer raven-*-win*-setup.exe back to the Ubuntu18 build machine to the folder ~/release (to shasum with the rest of the releases)
```



# MacOS prep: Make your SDK #
From an ubuntu 18 bionic server(required)
```
cd ~/
sudo apt install git p7zip-full sleuthkit
git clone https://github.com/ravenproject/ravencoin
mkdir ~/dmg && cd ~/dmg
#Register for a developer account with Apple, then download the Xcode 7.3.1 dmg from: https://developer.apple.com/devcenter/download.action?path=/Developer_Tools/Xcode_9.4.1/Xcode_9.4.1.dmg
#Transfer Xcode_7.3.1.dmg to the target machine into ~/dmg 
~/ravencoin/contrib/macdeploy/extract-osx-sdk.sh
rm -rf 5.hfs MacOSX10.11.sdk Xcode_7.3.1.dmg
# Save MacOSX10.11.sdk.tar.gz somewhere safe for future builds
```



# Static MacOS builds #
From an ubuntu 18 bionic server(required)
```
cd ~/
export PATH_orig=$PATH
DISTNAME=raven-2.0.1
sudo apt install ca-certificates curl g++ git pkg-config autoconf librsvg2-bin libtiff-tools libtool automake bsdmainutils cmake imagemagick libcap-dev libz-dev libbz2-dev python python-dev python-setuptools fonts-tuffy
git clone https://github.com/ravenproject/ravencoin
mkdir ~/ravencoin/depends/SDKs
#transfer MacOSX10.11.sdk.tar.gz to the folder ravencoin/depends/SDKs
cd ravencoin/depends/SDKs && tar -xf MacOSX10.11.sdk.tar.gz 
rm MacOSX10.11.sdk.tar.gz 
cd ~/ravencoin/depends
make -j4 HOST="x86_64-apple-darwin14"
cd ~/ravencoin
sudo ./autogen.sh
CONFIG_SITE=$PWD/depends/x86_64-apple-darwin14/share/config.site ./configure --prefix=/ --disable-ccache --disable-maintainer-mode --disable-dependency-tracking --enable-reduce-exports --disable-bench --disable-gui-tests GENISOIMAGE=$PWD/depends/x86_64-apple-darwin14/native/bin/genisoimage
make -j4 
mkdir ~/OSX
export PATH=$PWD/depends/x86_64-apple-darwin14/native/bin:$PATH
make install-strip DESTDIR=~/OSX/$DISTNAME
make osx_volname
make deploydir
mkdir -p unsigned-app-$DISTNAME
cp osx_volname unsigned-app-$DISTNAME/
cp contrib/macdeploy/detached-sig-apply.sh unsigned-app-$DISTNAME
cp contrib/macdeploy/detached-sig-create.sh unsigned-app-$DISTNAME
cp $PWD/depends/x86_64-apple-darwin14/native/bin/dmg $PWD/depends/x86_64-apple-darwin14/native/bin/genisoimage unsigned-app-$DISTNAME
cp $PWD/depends/x86_64-apple-darwin14/native/bin/x86_64-apple-darwin14-codesign_allocate unsigned-app-$DISTNAME/codesign_allocate
cp $PWD/depends/x86_64-apple-darwin14/native/bin/x86_64-apple-darwin14-pagestuff unsigned-app-$DISTNAME/pagestuff
mv dist unsigned-app-$DISTNAME
cd unsigned-app-$DISTNAME
find . | sort | tar --no-recursion --mode='u+rw,go+r-w,a+X' --owner=0 --group=0 -c -T - | gzip -9n > ~/sign/$DISTNAME-osx-unsigned.tar.gz
cd ~/ravencoin
make deploy
$PWD/depends/x86_64-apple-darwin14/native/bin/dmg dmg "Raven-Core.dmg" ~/release/unsigned/$DISTNAME-osx-unsigned.dmg
sudo rm -rf unsigned-app-$DISTNAME dist osx_volname dpi36.background.tiff dpi72.background.tiff
cd ~/OSX
find . -name "lib*.la" -delete
find . -name "lib*.a" -delete
rm -rf $DISTNAME/lib/pkgconfig
find $DISTNAME | sort | tar --no-recursion --mode='u+rw,go+r-w,a+X' --owner=0 --group=0 -c -T - | gzip -9n > ~/release/$DISTNAME-osx64.tar.gz
cd ~/ravencoin
rm -rf ~/OSX
make clean
export PATH=$PATH_orig
# Transfer ~/sign/raven-*-osx*-unsigned.tar.gz to a MacOS signing machine
```



# Signing MacOS builds #
From a Apple MacOS device: open terminal
```
DISTNAME=raven-2.0.1
xcode-select --install
cd ~/desktop
mkdir OSX
#transfer raven-*-osx-unsigned.tar.gz to ~/desktop/OSX
cd OSX
tar xf $DISTNAME-osx-unsigned.tar.gz
#acquire a code signing certifacte from apple follwoing the instructions here:
#https://developer.apple.com/library/archive/documentation/Security/Conceptual/CodeSigningGuide/Procedures/Procedures.html#//apple_ref/doc/uid/TP40005929-CH4-SW2
#where  "Key ID" is the name of the private key used to generate your codesigning certificate
./detached-sig-create.sh -s "Key ID" 
#Enter the keychain password and authorize the signature
#Copy signature-osx.tar.gz back to the ubuntu 18 build machine
```
From the ubuntu 18 bionic server(required) build machine:
```
DISTNAME=raven-2.0.1
cd ~/sign
mkdir OSX/
cp $DISTNAME-osx-unsigned.tar.gz OSX
cd OSX
tar -xf $DISTNAME-osx-unsigned.tar.gz 
# transfer signature-osx.tar.gz to ~/sign/OSX
tar -xf signature-osx.tar.gz
OSX_VOLNAME="$(cat osx_volname)"
./detached-sig-apply.sh $DISTNAME-osx-unsigned.tar.gz osx
./genisoimage -no-cache-inodes -D -l -probe -V "${OSX_VOLNAME}" -no-pad -r -dir-mode 0755 -apple -o uncompressed.dmg signed-app
./dmg dmg uncompressed.dmg ~/release/$DISTNAME-osx.dmg
cd ~/sign
rm -rf OSX
```



# Checksum all the builds #
From the ubuntu 18 bionic server(required) build machine
```
DISTNAME=raven-2.0.1
#transfer your *-secret-gpg.key and *-ownertrust-gpg.txt to ~/
#import your PGP keys
gpg --import *-secret-gpg.key
#enter your password
gpg --import-ownertrust *-ownertrust-gpg.txt
cd ~/release
sha256sum $DISTNAME.tar.gz > SHA256SUMS
sha256sum $DISTNAME-aarch64-linux-gnu.tar.gz >> SHA256SUMS
sha256sum $DISTNAME-arm-linux-gnueabihf.tar.gz >> SHA256SUMS
sha256sum $DISTNAME-i686-pc-linux-gnu.tar.gz >> SHA256SUMS
sha256sum $DISTNAME-osx.dmg >> SHA256SUMS
sha256sum $DISTNAME-osx64.tar.gz >> SHA256SUMS
sha256sum $DISTNAME-win32.zip >> SHA256SUMS
sha256sum $DISTNAME-win32-setup.exe >> SHA256SUMS
sha256sum $DISTNAME-win64.zip >> SHA256SUMS
sha256sum $DISTNAME-win64-setup.exe >> SHA256SUMS
sha256sum $DISTNAME-x86_64-linux-gnu.tar.gz >> SHA256SUMS
gpg --digest-algo sha256 --clearsign SHA256SUMS
#enter your password 
rm SHA256SUMS
# Upload all the releases and SHA256SUMS.asc to github
```


