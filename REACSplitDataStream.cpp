/*
 *  REACSplitDataStream.cpp
 *  REAC
 *
 *  Created by Per Eckerdal on 18/03/2011.
 *  Copyright 2011 Per Eckerdal. All rights reserved.
 *
 */

#include "REACSplitDataStream.h"

#define super REACDataStream

#include "REACConnection.h"

OSDefineMetaClassAndStructors(REACSplitDataStream, super)

bool REACSplitDataStream::initConnection(REACConnection *conn) {
    splitHandshakeState = HANDSHAKE_NOT_INITIATED;
    
    return super::initConnection(conn);
}

bool REACSplitDataStream::gotPacket(const REACPacketHeader *packet, const EthernetHeader *header) {
    if (super::gotPacket(packet, header)) {
        return true;
    }
    
    bool result = false;
    
    if (0 == memcmp(packet->type,
                    STREAM_TYPE_IDENTIFIERS[REAC_STREAM_MASTER_ANNOUNCE],
                    sizeof(STREAM_TYPE_IDENTIFIERS[0]))) {
        MasterAnnouncePacket *map = (MasterAnnouncePacket *)packet->data;
        if (HANDSHAKE_NOT_INITIATED == splitHandshakeState) {
            if (0x0d == map->unknown1[6]) {
                memcpy(splitMasterDevice.addr, map->address, sizeof(splitMasterDevice.addr));
                splitMasterDevice.in_channels = map->inChannels;
                splitMasterDevice.out_channels = map->outChannels;
                splitHandshakeState = HANDSHAKE_GOT_MASTER_ANNOUNCE;
            }
            result = true;
        }
        else if (HANDSHAKE_SENT_FIRST_ANNOUNCE == splitHandshakeState) {
            if (0x0a == map->unknown1[6]) {
                if (0 == connection->interfaceAddrCmp(sizeof(map->address), map->address)) {
                    splitIdentifier = map->outChannels;
                    splitHandshakeState = HANDSHAKE_GOT_SECOND_MASTER_ANNOUNCE;
                }
            }
            result = true;
        }
    }
    
    return result;
}

bool REACSplitDataStream::prepareSplitAnnounce(REACPacketHeader *packet) {
    bool ret = false;
    
    memcpy(packet->type, STREAM_TYPE_IDENTIFIERS[REACDataStream::REAC_STREAM_SPLIT_ANNOUNCE], sizeof(packet->type));
    
    if (HANDSHAKE_GOT_MASTER_ANNOUNCE == splitHandshakeState) {
        memset(packet->data, 0, sizeof(packet->data));
        packet->data[0] = 0x01;
        packet->data[1] = 0x00;
        packet->data[2] = 0x7f;
        packet->data[3] = 0x00;
        packet->data[4] = 0x01;
        packet->data[5] = 0x03;
        packet->data[6] = 0x08;
        packet->data[7] = 0x43;
        packet->data[8] = 0x05;
        
        connection->getInterfaceAddr(ETHER_ADDR_LEN, packet->data+9 /* sorry about the magic constant */);
        ret = true;
        splitHandshakeState = HANDSHAKE_SENT_FIRST_ANNOUNCE;
    }
    else if (HANDSHAKE_GOT_SECOND_MASTER_ANNOUNCE == splitHandshakeState) {
        memset(packet->data, 0, sizeof(packet->data));
        packet->data[0] = 0x01;
        packet->data[1] = 0x00;
        packet->data[2] = splitIdentifier;
        packet->data[3] = 0x00;
        packet->data[4] = 0x01;
        packet->data[5] = 0x03;
        packet->data[6] = 0x08;
        packet->data[7] = 0x42;
        packet->data[8] = 0x05;
        
        connection->getInterfaceAddr(ETHER_ADDR_LEN, packet->data+9 /* sorry about the magic constant */);
        ret = true;
        splitHandshakeState = HANDSHAKE_CONNECTED;
    }
    else if (HANDSHAKE_CONNECTED == splitHandshakeState) {
        memset(packet->data, 0, sizeof(packet->data));
        packet->data[0] = 0x01;
        packet->data[1] = 0x00;
        packet->data[2] = splitIdentifier;
        packet->data[3] = 0x00;
        packet->data[4] = 0x01;
        packet->data[5] = 0x03;
        packet->data[6] = 0x02;
        packet->data[7] = 0x41;
        packet->data[8] = 0x05;
        
        ret = true;
    }
    
    if (HANDSHAKE_NOT_INITIATED != splitHandshakeState && recievedPacketCounter == counterAtLastSplitAnnounce) {
        IOLog("REACDataStream::prepareSplitAnnounce(): Disconnect.\n"); // TODO Don't just announce in the log
        splitHandshakeState = HANDSHAKE_NOT_INITIATED;
        ret = false;
    }
    
    REACDataStream::applyChecksum(packet);
    
    counterAtLastSplitAnnounce = recievedPacketCounter;
    
    return ret;
}
