/*
 *  REACSlaveDataStream.h
 *  REAC
 *
 *  Created by Per Eckerdal on 18/03/2011.
 *  Copyright 2011 Per Eckerdal. All rights reserved.
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
};

#endif
