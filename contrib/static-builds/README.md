Automated/Manual Build Process
==============

Either follow the manual steps in:  
[Staticbuild.md](/static-builds/staticbuild.md)  

-OR-  


Preparation Steps for Build Process
---------------------

### OSX prep: Make your SDK 
From an Ubuntu 18 bionic server(required)

```
cd ~/
sudo apt install git p7zip-full sleuthkit
git clone https://github.com/ravenproject/ravencoin
mkdir ~/dmg && cd ~/dmg
```
Register for a developer account with Apple, then download the Xcode 7.3.1 dmg from:   https://developer.apple.com/devcenter/download.action?path=/Developer_Tools/Xcode_7.3.1/Xcode_7.3.1.dmg  
Transfer Xcode_7.3.1.dmg to the target machine into ~/dmg 

```
~/ravencoin/contrib/macdeploy/extract-osx-sdk.sh
rm -rf 5.hfs MacOSX10.11.sdk Xcode_7.3.1.dmg
```

Save MacOSX10.11.sdk.tar.gz to the ~/ folder of your Ubuntu 18 (bionic) build machine

Static Build process
---------------------

Run the automated shell script from ~/ on an Ubuntu18 machine:  
[script.sh](/static-builds/script.sh)


The following variables allow you to change parameters of the build:
```
DISTNAME
```
(to change the build number of the release)


```
MAKEOPTS
```
(to specify the  number of cores you woulf like to compile on `-jX` where X is the number of cores)


```
BRANCH
```
(to specify the branch you would like to build from)


### Post Script.sh step

Because the script runs as root, you may need to `$ sudo chown -R someusername:somegroup ~/sign ~/release`   
to comfortably perform the next steps.


Signing Binaries
---------------------

### Signing Windows 64 binaries
From an Ubuntu 16.04 xenial machine- !!!IMPORTANT (openssl 1.0.2 required)  
This process requires core to have a pvk file (kept secret)and a cert in PEM format(from comodo) uploaded to the repo at contrib/windeploy/win-codesign.cert

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
```
Transfer raven-*-win*-unsigned.tar.gz to ~/win64/ from the Ubuntu 18 build machine's ~/sign directory  
```
tar xf $DISTNAME-win*-unsigned.tar.gz
rm $DISTNAME-win*-unsigned.tar.gz
./detached-sig-create.sh -key /path/to/codesign.pvk
```
Enter the passphrase for the key when prompted
```
tar xf signature-win.tar.gz 
osslsigncode attach-signature -in "unsigned/$DISTNAME-win64-setup-unsigned.exe" -out "$DISTNAME-win64-setup.exe" -sigin "win/$DISTNAME-win64-setup-unsigned.exe.pem"
```
Transfer raven-*-win*-setup.exe back to the Ubuntu18 build machine to the folder ~/release (to shasum with the rest of the releases)




### Signing Windows 32 binaries
From an Ubuntu 16.04 xenial machine !important (openssl 1.0.2 required)  
This process requires core to have a pvk file (kept secret)and a cert in PEM format(from comodo) uploaded to the repo at contrib/windeploy/win-codesign.cert

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
```
Transfer raven-*-win*-unsigned.tar.gz to ~/win32/ from the Ubuntu 18 build machine's ~/sign directory  
```
tar xf $DISTNAME-win*-unsigned.tar.gz
rm $DISTNAME-win*-unsigned.tar.gz
./detached-sig-create.sh -key /path/to/codesign.pvk
```
Enter the passphrase for the key when prompted
```
tar xf signature-win.tar.gz 
osslsigncode attach-signature -in "unsigned/$DISTNAME-win32-setup-unsigned.exe" -out "$DISTNAME-win32-setup.exe" -sigin "win/$DISTNAME-win32-setup-unsigned.exe.pem"
```
Transfer raven-*-win*-setup.exe back to the Ubuntu18 build machine to the folder ~/release (to shasum with the rest of the releases)




### Signing MacOS binaries
From a Apple MacOS device: open terminal

```
DISTNAME=raven-2.0.1
xcode-select --install
cd ~/desktop
mkdir OSX
```
Transfer raven-*-osx-unsigned.tar.gz to ~/desktop/OSX from the Ubuntu 18 build machine's ~/sign directory 
```
cd OSX
tar xf $DISTNAME-osx-unsigned.tar.gz
```
Acquire a code signing certifacte from apple follwoing the instructions here:
https://developer.apple.com/library/archive/documentation/Security/Conceptual/CodeSigningGuide/Procedures/Procedures.html#//apple_ref/doc/uid/TP40005929-CH4-SW2

Where  "Key ID" is the name of the private key used to generate your codesigning certificate
```
./detached-sig-create.sh -s "Key ID" 
```
Enter the keychain password and authorize the signature  
Copy signature-osx.tar.gz back to the ubuntu 18 build machine

From the ubuntu 18 bionic server(required) build machine
```
DISTNAME=raven-2.0.1
cd ~/sign
mkdir OSX/
cp $DISTNAME-osx-unsigned.tar.gz OSX
cd OSX
tar -xf $DISTNAME-osx-unsigned.tar.gz 
```
Transfer signature-osx.tar.gz to ~/sign/OSX from the MacOS device
```
tar -xf signature-osx.tar.gz
OSX_VOLNAME="$(cat osx_volname)"
./detached-sig-apply.sh $DISTNAME-osx-unsigned.tar.gz osx
./genisoimage -no-cache-inodes -D -l -probe -V "${OSX_VOLNAME}" -no-pad -r -dir-mode 0755 -apple -o uncompressed.dmg signed-app
./dmg dmg uncompressed.dmg ~/release/$DISTNAME-osx.dmg
cd ~/sign
rm -rf OSX
```


Checksum all the binaries
---------------------

From the ubuntu 18 bionic server(required) build machine
```
DISTNAME=raven-2.0.1
```
Transfer your *-secret-gpg.key and *-ownertrust-gpg.txt to ~/  
Import your PGP keys
```
gpg --import *-secret-gpg.key
```
Enter your password
```
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
```
Enter your password 
```
rm SHA256SUMS
```

Upload all the releases and SHA256SUMS.asc to github



