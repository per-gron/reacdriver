/*
 *  REACMasterDataStream.h
 *  REAC
 *
 *  Created by Per Eckerdal on 18/03/2011.
 *  Copyright 2011 Per Eckerdal. All rights reserved.
 *
 */

#ifndef _REACMASTERDATASTREAM_H
#define _REACMASTERDATASTREAM_H

#include "REACDataStream.h"

class REACMasterDataStream : public REACDataStream {
    OSDeclareDefaultStructors(REACMasterDataStream)
public:
    
    virtual bool initConnection(com_pereckerdal_driver_REACConnection *conn);
    
protected:
    
    // Object destruction method that is used by free, and init on failure.
    virtual void deinit();
    virtual void free();
    
public:
    
    // Return kIOReturnSuccess on success, kIOReturnAborted if no packet should be sent, and anything else on error.
    //virtual IOReturn processPacket(REACPacketHeader *packet, UInt32 dhostLen, UInt8 *dhost);
    //virtual void gotPacket(const REACPacketHeader *packet, const com_pereckerdal_driver_EthernetHeader *header);
};

#endif
