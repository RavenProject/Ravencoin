**Steps to be performed before any Pull Request is accepted into the master branch**

  1. Check PROTOCOL_VERSION in the following location: src/version.h

  2. Check Ravend Version in the following locations: configure.ac, src/version.h

  3. All unit and functional tests pass

  4. Check PROTOCOL_VERSION in the iOS app located at ravenwallet-ios

  5. Check PROTOCOL_VERSION is the android app located at ravenwallet-android

  6. Check the Javascript stack (ravencore) for any block serialization or rpc changes
  
  7. Build release notes for all new features and bug fixes

**If hard fork:**

  1. Notify all exchanges, pools, and wallets of release of critical update

**Post Release :**

  1. Update ravencoin.org with correct popup version
  
  2. Update ravencoin.org with correct release download urls for each platform (Windows, Linux, Mac)

**Build Process**

  1. Verify that the release build doesn't say ***dirty*** in the commit message

