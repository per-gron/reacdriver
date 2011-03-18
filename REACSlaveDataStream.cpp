/*
 *  REACSlaveDataStream.cpp
 *  REAC
 *
 *  Created by Per Eckerdal on 18/03/2011.
 *  Copyright 2011 Per Eckerdal. All rights reserved.
 *
 */

#include "REACSlaveDataStream.h"

#include "REACConnection.h"

#define super REACDataStream

OSDefineMetaClassAndStructors(REACSlaveDataStream, super)

bool REACSlaveDataStream::initConnection(REACConnection *conn) {
    return super::initConnection(conn);
}

IOReturn REACSlaveDataStream::processPacket(REACPacketHeader *packet, UInt32 dhostLen, UInt8 *dhost) {
    return super::processPacket(packet, dhostLen, dhost);
}

bool REACSlaveDataStream::gotPacket(const REACPacketHeader *packet, const EthernetHeader *header) {
    if (super::gotPacket(packet, header)) {
        return true;
    }
    
    return false;
}
