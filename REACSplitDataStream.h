/*
 *  REACSplitDataStream.h
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

#ifndef _REACSPLITDATASTREAM_H
#define _REACSPLITDATASTREAM_H

#include "REACDataStream.h"

#define REACSplitDataStream    com_pereckerdal_driver_REACSplitDataStream

class REACSplitDataStream : public REACDataStream {
    OSDeclareDefaultStructors(REACSplitDataStream)

protected:
    
    virtual bool initConnection(com_pereckerdal_driver_REACConnection *conn);
    
public:
    virtual bool gotPacket(const REACPacketHeader *packet, const com_pereckerdal_driver_EthernetHeader *header);
    
    // Returns true if a packet should be sent
    bool prepareSplitAnnounce(REACPacketHeader *packet);
    
protected:
    enum HandshakeState {
        HANDSHAKE_NOT_INITIATED,
        HANDSHAKE_GOT_MASTER_ANNOUNCE,
        HANDSHAKE_SENT_FIRST_ANNOUNCE,
        HANDSHAKE_GOT_SECOND_MASTER_ANNOUNCE,
        HANDSHAKE_CONNECTED
    };
    HandshakeState      handshakeState;
    REACDeviceInfo      masterDevice;
    UInt8               splitIdentifier;
    UInt64              counterAtLastSplitAnnounce;
};


#endif
