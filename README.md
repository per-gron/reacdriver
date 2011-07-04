# Mac OS X REAC driver

This is a CoreAudio driver that make REAC devices show up as audio interfaces within Mac OS X.
At the moment, the driver acts as a REAC split device, which means that it is possible to listen
to REAC devices, and they acknowledge the connection, so it is possible to use Roland's control
software to change preamp settings etc without connecting the master device to another Roland unit.

The driver currently has several important limitations including that it only is tested with the
S-1608, it is hard coded to split mode (even though incomplete support for master mode is
implemented and a stub for slave mode is there). The driver is hard coded to listen to devices
with 16 inputs. This limitation should not be difficult to remedy, but I see little benefit to
doing so at the moment, since I only have access to the S-1608.

I have partly been able to reverse engineer the REAC protocol, and have succeeded to implement
slave and master mode, but there is still at least one step left in the slave handshake.
Additionally, this code is as dumb as possible with the metadata part of the REAC packets. It
basically does nothing with it except when it's necessary to finish the connection handshake.

# Usage

XCode is used to build the driver. The product, a directory called `REAC.kext`, will be located
somewhere within the build directory, depending on your build settings. For development the
scripts `test/load.sh` and `test/unload.sh` are useful (they are simple wrapper scripts around
`kextload`).

To install the driver permanently, copy `REAC.kext` to `/System/Library/Extensions`

# Use at your own risk!

This is not very thouroughly tested kernel code. Installing this code on your computer might
make it crash, it might erase all the contents on your file system. Kernel code is even able
to permanently damage the hardware of your computer.

I believe and hope that this code won't do any of these things, but there are no guarantees.
Please see the file COPYING for further details.

# License

This code (except Apple's PCMBlitterLib, which is optimized code to convert between floating
and fixed point PCM data) is released under the General Public License version 3. See the file
COPYING for the full license
