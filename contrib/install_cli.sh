 #!/usr/bin/env bash

 # Execute this file to install the yottaflux cli tools into your path on OS X

 CURRENT_LOC="$( cd "$(dirname "$0")" ; pwd -P )"
 LOCATION=${CURRENT_LOC%Yottaflux-Qt.app*}

 # Ensure that the directory to symlink to exists
 sudo mkdir -p /usr/local/bin

 # Create symlinks to the cli tools
 sudo ln -s ${LOCATION}/Yottaflux-Qt.app/Contents/MacOS/yottafluxd /usr/local/bin/yottafluxd
 sudo ln -s ${LOCATION}/Yottaflux-Qt.app/Contents/MacOS/yottaflux-cli /usr/local/bin/yottaflux-cli
