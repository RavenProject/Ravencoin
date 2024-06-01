macOS Migration Instructions and Notes
====================================

Motivation
-----------
This guide is addressing the issue of increasing RavenCore-QT.app (to be exact - Application Support directory) size when installed on mac's internal storage. 

The outcome of this guide is to help with safely transferring the Application Support directory and the app itself to an external storage.

Please use this guide if you have already installed Raven Wallet before using [this guide](https://github.com/RavenProject/Ravencoin/blob/master/doc/build-osx.md)

Pre-Requisites
-----------
1. Your RavenCore-QT.app is installed in internal storage
2. You have found the Application Support directory path ([navigate to this link](https://github.com/RavenProject/Ravencoin/blob/master/doc/build-osx.md#running) to refresh your memory)
3. You have backed-up all the data from wallet (12-phrase, etc.)
4. Your external storage has APFS format (no tests have been conducted for other file formats like FAT, NTFS, EXT, etc.)
5. The external storage should be at least 100+ GiB

Guide
-----------
Connect your external storage to your mac. Confirm that the OS has detected the storage

### Transfer RavenCore-QT.App and Application Support Directory
1. Open Finder and navigate to the Application Folder
2. Drag and Drop Raven App to your external storage
3. Navigate to your Application Support directory
4. Copy the Application Support directory to the external storage

### AppleScript
1. Open Spotlight search
2. Find and open AppleScript Editor(or Script Editor)
3. In the menu bar find "File"
4. Click on "New"
5. In the new file editor insert
```
do shell script "open -a '/Volumes/<Your_External_Storage_Name>/Raven-Qt.app' --args -datadir=/Volumes/<Your_External_Storage_Name>/Application\ Support"
```
Note: this example above is assuming your have copied App and support directory to the root path of your external storage. Edit the path if your path is different

6. Save the script (In menu bar File->Save)
7. Export the script to app (Export->File Format: App)
8. Pin your new app to the dock
9. Click on the app you have just created using AppleScript to verify that your RavenWallet is working properly as before

### Finish
If you have successully achieved all steps above, you can remove the RavenCore-QT.app in your internal storage -> Applications folder and internal storage -> Application Support Directory

<b>Please don't do it</b> if you have had issues along the way. Open an issue for the community's help

Notes
-----

* Tested on OS X 10.15.7 on 64-bit Intel processors only.

* Tested on 1 TiB NVMe external storage formated to APFS
