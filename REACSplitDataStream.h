/*
 *  REACSplitDataStream.h
 *  REAC
 *
 *  Created by Per Eckerdal on 18/03/2011.
 *  Copyright 2011 Per Eckerdal. All rights reserved.
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
    enum SplitHandshakeState {
        HANDSHAKE_NOT_INITIATED,
        HANDSHAKE_GOT_MASTER_ANNOUNCE,
        HANDSHAKE_SENT_FIRST_ANNOUNCE,
        HANDSHAKE_GOT_SECOND_MASTER_ANNOUNCE,
        HANDSHAKE_CONNECTED
    };
    SplitHandshakeState splitHandshakeState;
    REACDeviceInfo      splitMasterDevice;
    UInt8               splitIdentifier;
    UInt64              counterAtLastSplitAnnounce;
};


#endif
