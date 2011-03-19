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
    resetHandshakeState();
    lastCdeaTwoBytes[0] = lastCdeaTwoBytes[1] = 0;
    
    return super::initConnection(conn);
}

IOReturn REACSlaveDataStream::processPacket(REACPacketHeader *packet, UInt32 dhostLen, UInt8 *dhost) {

#   define setPacketTypeMacro(packetType) \
        memcpy(packet->type, STREAM_TYPE_IDENTIFIERS[packetType], sizeof(packet->type));

    super::processPacket(packet, dhostLen, dhost);
    
    packet->setCounter(counter);
    
    if (sizeof(masterDevice.addr) != dhostLen) {
        return kIOReturnError;
    }
    
    IOReturn ret = kIOReturnAborted;
    
    if (HANDSHAKE_GOT_MAC_ADDRESS_INFO == handshakeState) {
        memset(dhost, 0xff, dhostLen);
        setPacketTypeMacro(REAC_STREAM_FILLER);
        memset(packet->data, 0, sizeof(packet->data));
        ret = kIOReturnSuccess;
    }
    else if (HANDSHAKE_SENDING_INITIAL_ANNOUNCE == handshakeState) {
        const UInt8 handshakeData[][19] = {
            { 0x00, 0x00, 0x02, 0x02, 0x02, 0x02, 0x02, 0x01, 0x01, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x00, 0x00, 0x00, 0x00 },
            { 0x02, 0x00, 0xfe, 0x0f, 0xf0, 0x41, 0x0a, 0x00, 0x00, 0x12, 0x12, 0x01, 0x00, 0x06, 0x00, 0x01, 0x00, 0x78, 0xf7 },
            { 0x02, 0x00, 0xfe, 0x0f, 0xf0, 0x41, 0x0a, 0x00, 0x00, 0x12, 0x12, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x7d, 0xf7 },
            { 0x02, 0x00, 0xfe, 0x0e, 0xf0, 0x41, 0x0a, 0x00, 0x00, 0x12, 0x12, 0x03, 0x02, 0x00, 0x01, 0x00, 0x7a, 0xf7, 0x00 },
            { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
        };
        
        memcpy(dhost, masterDevice.addr, dhostLen);
        setPacketTypeMacro(REAC_STREAM_CONTROL);
        
        if (0 == handshakeSubState) {
            memset(packet->data, 0, sizeof(packet->data));
        }
        else {
            memcpy(packet->data,
                   REAC_STREAM_CONTROL_PACKET_TYPE[CONTROL_PACKET_TYPE_SLAVE_ANNOUNCE1+handshakeSubState-1],
                   REAC_STREAM_CONTROL_PACKET_TYPE_SIZE);
            memcpy(packet->data+REAC_STREAM_CONTROL_PACKET_TYPE_SIZE,
                   handshakeData[handshakeSubState-1],
                   sizeof(handshakeData[0]));
        }
        
        if (5 == handshakeSubState) {
            setHandshakeState(HANDSHAKE_HAS_SENT_ANNOUNCE);
        }
        else {
            handshakeSubState++;
        }
        
        applyChecksum(packet);
        lastCdeaTwoBytes[0] = packet->data[sizeof(packet->data)-2];
        lastCdeaTwoBytes[1] = REACDataStream::applyChecksum(packet);
        ret = kIOReturnSuccess;
    }
    else if (HANDSHAKE_HAS_SENT_ANNOUNCE == handshakeState) {
        memcpy(dhost, masterDevice.addr, dhostLen);
        setPacketTypeMacro(REAC_STREAM_FILLER);
        for (int i=0; i<31; i+=2) {
            packet->data[i+0] = lastCdeaTwoBytes[0];
            packet->data[i+1] = lastCdeaTwoBytes[1];
        }
        ret = kIOReturnSuccess;
    }
    
    return ret;
}

bool REACSlaveDataStream::gotPacket(const REACPacketHeader *packet, const EthernetHeader *header) {
    if (super::gotPacket(packet, header)) {
        return true;
    }
    
    if (HANDSHAKE_NOT_INITIATED == handshakeState) {
        if (0 == handshakeSubState) {
            if (isControlPacketType(packet, CONTROL_PACKET_TYPE_ONE) &&
                0xc0 == packet->data[29] &&
                0xa8 == packet->data[30]) {
                handshakeSubState = 1;
            }
        }
        else if (1 == handshakeSubState &&
                 isControlPacketType(packet, CONTROL_PACKET_TYPE_ONE) &&
                 0x01 == packet->data[5] &&
                 0x01 == packet->data[6] &&
                 0 == memcmp(packet->data+7, packet->data+17, ETHER_ADDR_LEN)) {
            memcpy(masterDevice.addr, packet->data+7, sizeof(masterDevice.addr));
            setHandshakeState(HANDSHAKE_GOT_MAC_ADDRESS_INFO);
            lastGotMacAddressInfoStateUpdate = recievedPacketCounter;
            firstChannelInfoId = -1;
        }
        else {
            resetHandshakeState();
        }
    }
    else if (HANDSHAKE_GOT_MAC_ADDRESS_INFO == handshakeState) {
        if ((SInt64)recievedPacketCounter-(SInt64)lastGotMacAddressInfoStateUpdate > 2*REAC_PACKETS_PER_SECOND) {
            resetHandshakeState();
        }
        else if (isControlPacketType(packet, CONTROL_PACKET_TYPE_THREE)) {
            lastGotMacAddressInfoStateUpdate = recievedPacketCounter;
            
            if (true) {
                setHandshakeState(HANDSHAKE_SENDING_INITIAL_ANNOUNCE);
            }
            else {
                for (UInt32 i=5; i<8*3; i+=3) {
                    if (-1 == firstChannelInfoId) {
                        firstChannelInfoId = packet->data[i];
                    }
                    else if (firstChannelInfoId == packet->data[i]) {
                        setHandshakeState(HANDSHAKE_SENDING_INITIAL_ANNOUNCE);
                    }
                }
            }
        }
    }
    
    return false;
}

void REACSlaveDataStream::setHandshakeState(HandshakeState state) {
    IOLog("REACSlaveDataStream::setHandshakeState(): Set handshake state: %d\n", state); // TODO Debug
    handshakeState = state;
    handshakeSubState = 0;
}

void REACSlaveDataStream::resetHandshakeState() {
    setHandshakeState(HANDSHAKE_NOT_INITIATED);
}
