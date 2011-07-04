/*
 *  REACSlaveDataStream.h
 *  REAC
 *
 *  Created by Per Eckerdal on 18/03/2011.
 *  Copyright 2011 Per Eckerdal. All rights reserved.
 *  
 *  
 *  This file is part of the OS X REAC driver.
 *  
 *  The OS X REAC driver is free software: you can redistribute it
 *  and/or modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation, either version 3 of
 *  the License, or (at your option) any later version.
 *  
 *  The OS X REAC driver is distributed in the hope that it will be
 *  useful, but WITHOUT ANY WARRANTY; without even the implied warranty
 *  of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *  
 *  You should have received a copy of the GNU General Public License
 *  along with OS X REAC driver.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#ifndef _REACSLAVEDATASTREAM_H
#define _REACSLAVEDATASTREAM_H

#include "REACDataStream.h"
#include "EthernetHeader.h"

#define REACSlaveDataStream    com_pereckerdal_driver_REACSlaveDataStream

class REACSlaveDataStream : public REACDataStream {
    OSDeclareDefaultStructors(REACSlaveDataStream)
    
protected:
    
    virtual bool initConnection(com_pereckerdal_driver_REACConnection *conn);
    
public:
    
    virtual IOReturn processPacket(REACPacketHeader *packet, UInt32 dhostLen, UInt8 *dhost);
    virtual bool gotPacket(const REACPacketHeader *packet, const EthernetHeader *header);
    
protected:
    enum HandshakeState {
        HANDSHAKE_NOT_INITIATED,
        HANDSHAKE_GOT_MAC_ADDRESS_INFO, // We need to be careful here so that we never get stuck. lastGotMacAddressInfo is to stop that.
        HANDSHAKE_SENDING_INITIAL_ANNOUNCE,
        HANDSHAKE_HAS_SENT_ANNOUNCE,
        HANDSHAKE_CONNECTED
    };
    HandshakeState      handshakeState;
    UInt32              handshakeSubState;
    UInt64              lastGotMacAddressInfoStateUpdate;
    SInt32              firstChannelInfoId; // Is -1 before we got anything
    REACDeviceInfo      masterDevice;
    UInt8               lastCdeaTwoBytes[2];
    
    void setHandshakeState(HandshakeState state);
    void resetHandshakeState();
};

#endif
