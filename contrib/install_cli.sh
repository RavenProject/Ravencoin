 #!/usr/bin/env bash

 # Execute this file to install the raven cli tools into your path on OS X

 CURRENT_LOC="$( cd "$(dirname "$0")" ; pwd -P )"
 LOCATION=${CURRENT_LOC%Raven-Qt.app*}

 # Ensure that the directory to symlink to exists
 sudo mkdir -p /usr/local/bin

 # Create symlinks to the cli tools
 sudo ln -s ${LOCATION}/Raven-Qt.app/Contents/MacOS/ravend /usr/local/bin/ravend
 sudo ln -s ${LOCATION}/Raven-Qt.app/Contents/MacOS/raven-cli /usr/local/bin/raven-cli
