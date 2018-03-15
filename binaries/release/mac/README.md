# ChickadeeBinaries Mac Download Instructions

1) Download and copy chickadee.dmg to desired folder 

2) Double click the chickadee.dmg

3) Drag Chickadee Core icon to the Applications 

4) Launch Chickadee Core

Note: On Chickadee Core launch if you get this error

```
Dyld Error Message:
  Library not loaded: @loader_path/libboost_system-mt.dylib
  Referenced from: /Applications/Chickadee-Qt.app/Contents/Frameworks/libboost_thread-mt.dylib
  Reason: image not found
```
You will need to copy libboost_system.dylib to libboost_system-mt.dylib in the /Applications/Chickadee-Qt.app/Contents/Frameworks folder  
  