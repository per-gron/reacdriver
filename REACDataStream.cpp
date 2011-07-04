/*
 *  REACDataSteam.cpp
 *  REAC
 *
 *  Created by Per Eckerdal on 15/03/2011.
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

#include "REACDataStream.h"

#include <IOKit/IOLib.h>

#include "REACConnection.h"
#include "REACMasterDataStream.h"
#include "REACSlaveDataStream.h"
#include "REACSplitDataStream.h"

const UInt8 REACDataStream::STREAM_TYPE_IDENTIFIERS[][2] = {
    { 0x00, 0x00 }, // REAC_STREAM_FILLER
    { 0xcd, 0xea }, // REAC_STREAM_CONTROL
    { 0xcf, 0xea }, // REAC_STREAM_MASTER_ANNOUNCE
    { 0xce, 0xea }  // REAC_STREAM_SPLIT_ANNOUNCE
};


const UInt8 REACDataStream::REAC_STREAM_CONTROL_PACKET_TYPE[][REAC_STREAM_CONTROL_PACKET_TYPE_SIZE] = {
    { 0x01, 0x00, 0x00, 0x1a, 0x00 },
    { 0x01, 0x02, 0x00, 0x0e, 0x00 },
    { 0x01, 0x03, 0x00, 0x19, 0x01 },
    { 0x01, 0x01, 0x00, 0x18, 0x00 },
    
    // Slave announce types
    { 0x01, 0x03, 0x00, 0x10, 0x82 },
    { 0x04, 0x03, 0x00, 0x14, 0x00 },
    { 0x04, 0x03, 0x00, 0x13, 0x00 },
    { 0x01, 0x03, 0x00, 0x01, 0x81 }
};

#define super OSObject

OSDefineMetaClassAndStructors(REACDataStream, super)

bool REACDataStream::initConnection(REACConnection* conn) {
    connection = conn;
    lastAnnouncePacket = 0;
    counter = 0;
    recievedPacketCounter = 0;
    
    return true;
}

REACDataStream *REACDataStream::withConnection(REACConnection *conn) {
    REACDataStream *s = NULL;
    
    switch (conn->getMode()) {
        case REACConnection::REAC_MASTER:
            s = new REACMasterDataStream;
            break;
            
        case REACConnection::REAC_SLAVE:
            s = new REACSlaveDataStream;
            break;
            
        case REACConnection::REAC_SPLIT:
            s = new REACSplitDataStream;
            break;
    }
    
    if (NULL == s) return NULL;
    bool result = s->initConnection(conn);
    if (!result) {
        s->release();
        return NULL;
    }
    return s;
}

IOReturn REACDataStream::processPacket(REACPacketHeader *packet, UInt32 dhostLen, UInt8 *dhost) {
    counter++;
    
    return kIOReturnAborted;
}

bool REACDataStream::gotPacket(const REACPacketHeader *packet, const EthernetHeader *header) {
    recievedPacketCounter++;
    
    if (0 == memcmp(packet->type,
                    STREAM_TYPE_IDENTIFIERS[REAC_STREAM_FILLER],
                    sizeof(STREAM_TYPE_IDENTIFIERS[0]))) {
        return true;
    }
    
    if (!REACDataStream::checkChecksum(packet)) {
        IOLog("REACDataStream::gotPacket(): Got packet with invalid checksum.\n");
        return true;
    }
    
    /*IOLog("Got packet: "); // TODO Debug
     for (UInt32 i=0; i<sizeof(REACPacketHeader); i++) {
     IOLog("%02x", ((UInt8*)packet)[i]);
     }
     IOLog("\n");*/
    
    return false;
}

bool REACDataStream::checkChecksum(const REACPacketHeader *packet) {
    UInt8 expected_checksum = 0;
    for (UInt32 i=0; i<sizeof(packet->data); i++) {
        expected_checksum += packet->data[i];
    }
    return 0 == expected_checksum;
}

UInt8 REACDataStream::applyChecksum(REACPacketHeader *packet) {
    UInt8 sum = 0;
    for (int i=0; i<31; i++)
        sum += packet->data[i];
    sum = (256 - (int)sum);
    packet->data[31] = sum;
    return sum;
}

bool REACDataStream::isPacketType(const REACPacketHeader *packet, REACStreamType st) {
    return 0 == memcmp(packet->type, STREAM_TYPE_IDENTIFIERS[st], sizeof(STREAM_TYPE_IDENTIFIERS[st]));
}

bool REACDataStream::isControlPacketType(const REACPacketHeader *packet, REACStreamControlPacketType type) {
    return isPacketType(packet, REAC_STREAM_CONTROL) &&
    0 == memcmp(packet->data,
                REAC_STREAM_CONTROL_PACKET_TYPE[type],
                REAC_STREAM_CONTROL_PACKET_TYPE_SIZE);
}
