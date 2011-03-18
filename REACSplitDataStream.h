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

class REACSplitDataStream : public REACDataStream {
    OSDeclareDefaultStructors(REACSplitDataStream)
public:
    virtual bool initConnection(com_pereckerdal_driver_REACConnection *conn);
    
public:
    // Return kIOReturnSuccess on success, kIOReturnAborted if no packet should be sent, and anything else on error.
    //virtual IOReturn processPacket(REACPacketHeader *packet, UInt32 dhostLen, UInt8 *dhost);
    //virtual void gotPacket(const REACPacketHeader *packet, const com_pereckerdal_driver_EthernetHeader *header);
    
    // Returns true if a packet should be sent
    bool prepareSplitAnnounce(REACPacketHeader *packet);
};


#endif
