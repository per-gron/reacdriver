#!/bin/sh
sudo rm -rf REAC.kext
cp -R ../build/Development/REAC.kext .
# rm REAC.kext/REAC.kext
sudo chown -R root REAC.kext
sudo chgrp -R wheel REAC.kext
sudo kextload REAC.kext

