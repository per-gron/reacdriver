/*
 *  REACMasterDataStream.cpp
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

#include "REACMasterDataStream.h"

#include <IOKit/IOLib.h>

#include "REACConnection.h"

OSDefineMetaClassAndStructors(REACSplitUnit, OSObject)

bool REACSplitUnit::initAddress(UInt64 lastHeardFrom_, UInt8 identifier_, UInt32 addrLen, const UInt8 *addr) {
    lastHeardFrom = lastHeardFrom_;
    identifier = identifier_;
    
    if (sizeof(address) != addrLen) {
        return false;
    }
    memcpy(address, addr, addrLen);
    
    return true;
}

REACSplitUnit *REACSplitUnit::withAddress(UInt64 lastHeardFrom, UInt8 identifier, UInt32 addrLen, const UInt8 *addr) {
    REACSplitUnit *s = new REACSplitUnit;
    if (NULL == s) return NULL;
    bool result = s->initAddress(lastHeardFrom, identifier, addrLen, addr);
    if (!result) {
        s->release();
        return NULL;
    }
    return s;
}

#define super REACDataStream

OSDefineMetaClassAndStructors(REACMasterDataStream, super)

bool REACMasterDataStream::initConnection(REACConnection *conn) {
    lastCdeaTwoBytes[0] = lastCdeaTwoBytes[1] = 0;
    packetsUntilNextCdea = 0;
    cdeaState = 0;
    cdeaPacketsSinceStateChange = -1;
    cdeaAtChannel = 0;
    
    splitUnits = OSArray::withCapacity(10);
    if (NULL == splitUnits) {
        goto Fail;
    }
    masterGotSplitAnnounceState = GOT_SPLIT_NOT_INITIATED;
    
    slaveConnectionStatus = SLAVE_CONNECTION_NO_CONNECTION;
    gotSlaveAnnounce = false;
    
    return super::initConnection(conn);
    
Fail:
    deinit();
    return false;
}

void REACMasterDataStream::deinit() {
    if (NULL != splitUnits) {
        splitUnits->release();
        splitUnits = NULL;
    }
}

void REACMasterDataStream::free() {
    deinit();
    super::free();
}

IOReturn REACMasterDataStream::processPacket(REACPacketHeader *packet, UInt32 dhostLen, UInt8 *dhost) {
    
#   define setPacketTypeMacro(packetType) \
        memcpy(packet->type, STREAM_TYPE_IDENTIFIERS[packetType], sizeof(packet->type));
    
#   define applyChecksumAndSaveLastChecksumMacro() \
        { \
            lastCdeaTwoBytes[0] = packet->data[sizeof(packet->data)-2]; \
            lastCdeaTwoBytes[1] = REACDataStream::applyChecksum(packet); \
        }
    
    super::processPacket(packet, dhostLen, dhost);
    
    packet->setCounter(counter);
    
    memset(dhost, 0xff, dhostLen);
    
    if (GOT_SPLIT_ANNOUNCE == masterGotSplitAnnounceState) {
        static const UInt8 splitAnnounceResponse[] = {
            0xff, 0xff, 0x01, 0x00, 0x01, 0x03, 0x0a, 0x02, 0x02
        };
        
        masterGotSplitAnnounceState = GOT_SPLIT_SENT_SPLIT_ANNOUNCE_RESPONSE;
        
        SplitAnnounceResponsePacket *sarp = (SplitAnnounceResponsePacket *)packet->data;
        memcpy(sarp->unknown1, splitAnnounceResponse, sizeof(sarp->unknown1));
        memcpy(sarp->address,  masterSplitAnnounceAddr, sizeof(sarp->address));
        sarp->unknown2 = 0x00;
        sarp->identifierAssignment = 0x60; // 0x04 and up seems to be fine
        
        UInt8 *zeroPtr = packet->data+sizeof(splitAnnounceResponse)+sizeof(masterSplitAnnounceAddr)+2;
        memset(zeroPtr, 0, packet->data+sizeof(packet->data)-zeroPtr);
        
        setPacketTypeMacro(REAC_STREAM_MASTER_ANNOUNCE);
        REACDataStream::applyChecksum(packet);
        
        splitUnitConnected(sarp->identifierAssignment, sizeof(masterSplitAnnounceAddr), masterSplitAnnounceAddr);
    }
    else if (gotSlaveAnnounce) {
        gotSlaveAnnounce = false;
        setPacketTypeMacro(REAC_STREAM_CONTROL);
        memcpy(packet->data, slaveAnnounceData, sizeof(packet->data));
        applyChecksumAndSaveLastChecksumMacro();
        
        if (isControlPacketType(packet, CONTROL_PACKET_TYPE_SLAVE_ANNOUNCE3)) {
            // TODO This implementation is not complete
            slaveConnectionStatus = SLAVE_CONNECTION_GOT_SLAVE_ANNOUNCE;
        }
    }
    else if (counter-lastAnnouncePacket >= REAC_PACKETS_PER_SECOND) {
        static const UInt8 masterAnnounce[] = {
            0xff, 0xff, 0x01, 0x00, 0x01, 0x03, 0x0d, 0x01, 0x04
        };
        
        lastAnnouncePacket = counter;
        
        MasterAnnouncePacket *ap = (MasterAnnouncePacket *)packet->data;
        memcpy(ap->unknown1, masterAnnounce, sizeof(ap->unknown1));
        connection->getInterfaceAddr(sizeof(ap->address), ap->address);
        ap->inChannels = connection->getInChannels();
        ap->outChannels = connection->getOutChannels();
        
        if (REACMasterDataStream::isConnectedToSlave()) {
            // TODO This implementation is not complete
            ap->inChannels *= 2; // TODO Cap this at 40 channels?
            ap->outChannels *= 2;
        }
        
        ap->unknown2[0] = 0x01;
        // This byte has something to do with splits
        if (GOT_SPLIT_SENT_SPLIT_ANNOUNCE_RESPONSE == masterGotSplitAnnounceState) {
            masterGotSplitAnnounceState = GOT_SPLIT_NOT_INITIATED;
            ap->unknown2[1] = 0x01; // It does this. It doesn't seem to be necessary though.
        }
        else {
            ap->unknown2[1] = 0x00;
        }
        ap->unknown2[2] = 0x01;
        ap->unknown2[3] = 0x00;
        
        memset(packet->data+sizeof(MasterAnnouncePacket), 0, sizeof(packet->data)-sizeof(MasterAnnouncePacket));
        
        setPacketTypeMacro(REAC_STREAM_MASTER_ANNOUNCE);
        REACDataStream::applyChecksum(packet);
        
        disconnectObsoleteSplitUnits();
    }
    else if (0 >= packetsUntilNextCdea) {
        /// It is more or less impossible to read this code and understand what it does.
        /// It is because I don't understand it either. It basically attempts to output
        /// something that looks like the output of a real REAC unit.
        ///
        /// To make the implementation of it somewhat sane, I expressed it as a state
        /// machine.
        
#       define setCdeaStateMacro(state) \
            { \
                cdeaState = (state); \
                cdeaPacketsSinceStateChange = -1; \
            }
        
#       define incrementCdeaStateMacro() \
                setCdeaStateMacro(cdeaState+1);
        
#       define incrementCdeaStateMacroAfterNPacketsMacro(n) \
            if (cdeaPacketsSinceStateChange >= n-1) { \
                incrementCdeaStateMacro(); \
            }
        
#       define resetCdeaStateMacroAfterNPacketsMacro(n) \
            if (cdeaPacketsSinceStateChange >= n-1) { \
                setCdeaStateMacro(0); \
            }
        
        // Used when analyzing how the slave responds when not sending all types of cdea packets
#       define setCdeaStateMacroAfterNPacketsMacro(n, value) \
            if (cdeaPacketsSinceStateChange >= n-1) { \
                setCdeaStateMacro(value); \
            }
        
#       define fillPayloadMacro(initialOffset_, number_) \
            { \
                const SInt32 distance = 10; /* Distance between numbers */ \
                const UInt8 number = number_; \
                const SInt32 initialOffset = initialOffset_; \
                \
                memset(payload, 0, PAYLOAD_SIZE); \
                \
                if (0 == cdeaPacketsSinceStateChange) { \
                    cdeaCurrentOffset = initialOffset; \
                } \
                \
                while (cdeaCurrentOffset < PAYLOAD_SIZE) { \
                    payload[cdeaCurrentOffset] = number; \
                    cdeaCurrentOffset += distance; \
                } \
                cdeaCurrentOffset = cdeaCurrentOffset % PAYLOAD_SIZE; \
            }
        
        static const SInt32 PAYLOAD_SIZE = sizeof(packet->data)-REAC_STREAM_CONTROL_PACKET_TYPE_SIZE-1; // -2 for checksum
        UInt8 *payload = packet->data+REAC_STREAM_CONTROL_PACKET_TYPE_SIZE;
        
        const UInt8 afterChannelInfoPayload[] = {
            0x22, 0xc8, 0x31, 0x32, 0x33, 0x34, 0x01, 0x00, 0x00,
            0x00, 0x02, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00,
            0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00
        };
        
        const UInt8 afterAfterChannelInfoPayload[][PAYLOAD_SIZE] = {
            {
                0x00, 0x00, 0x02, 0x00, 0x19, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
                0x00, 0x0e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x19, 0x00
            },
            {
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x17, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x02, 0x00, 0x0f, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00
            },
            {
                0x02, 0x00, 0x1a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x18,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x20, 0x00, 0x00, 0x00
            }
        };
        
        const UInt8 stuffWithMACAddress[][18] = {
            { 0xc0, 0xa8, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff },
            { 0x17, 0x00, 0x00, 0x00, 0x1e, 0x4d, 0x00, 0x00, 0x53, 0x59, 0x53, 0x50, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00 },
            { 0x58, 0x56, 0x53, 0x43, 0x45, 0x4e, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
        };
        
        UInt32 cdeaPacketType = 0; // Default packet type
        
        ++cdeaPacketsSinceStateChange;
        packetsUntilNextCdea = 8; // Default number of packets until next cdea
        
        switch (cdeaState) {
            case 0: // Filler state
                fillPayloadMacro(2, 0x03);
                incrementCdeaStateMacroAfterNPacketsMacro(307);
                break;
                
            case 1: // Packet before channel info state
                cdeaPacketType = 1;
                memset(payload, 0, PAYLOAD_SIZE);
                payload[0]  = 0x03;
                payload[10] = 0x07;
                payload[11] = 0x65;
                payload[16] = 0x03;
                
                incrementCdeaStateMacro();
                packetsUntilNextCdea = 8018;
                
                break;
                
            case 2: // Channel info state
                cdeaPacketType = 2;
                for (int i=0; i<8; i++) {
                    // The first byte of the channel data is the channel number
                    if (cdeaAtChannel == 48) {
                        payload[i*3+0] = 0xfe;
                    }
                    else {
                        payload[i*3+0] = cdeaAtChannel;
                    }
                    
                    // The second byte of the channel data seems to contain channel type flags
                    // (input/output/none/terminator type + phantom)
                    if (cdeaAtChannel == 48) {
                        payload[i*3+1] = 0x01;
                    }
                    else if (cdeaAtChannel < connection->getInChannels()) {
                        payload[i*3+1] = 0x20;
                    }
                    else if (cdeaAtChannel < connection->getInChannels()+connection->getOutChannels()) {
                        payload[i*3+1] = 0x10;
                    }
                    else {
                        payload[i*3+1] = 0x30;
                    }
                    
                    // The third byte of the channel data is gain
                    payload[i*3+2] = 0x00;
                    
                    cdeaAtChannel = (cdeaAtChannel+1)%49;
                }
                memset(payload+PAYLOAD_SIZE-2, 0, 2);
                
                if (0 == cdeaPacketsSinceStateChange) {
                    packetsUntilNextCdea = 7947;
                }
                else {
                    incrementCdeaStateMacro();
                    packetsUntilNextCdea = 27;
                }
                
                break;
                
            case 3: // Packet after channel info state
                cdeaPacketType = 3;
                memcpy(payload, afterChannelInfoPayload, PAYLOAD_SIZE);
                
                incrementCdeaStateMacro();
                packetsUntilNextCdea = 4;
                
                break;
                
            case 4: // Stuff I don't understand after packet after channel info state
                memcpy(payload, afterAfterChannelInfoPayload[cdeaPacketsSinceStateChange], PAYLOAD_SIZE);
                incrementCdeaStateMacroAfterNPacketsMacro(3);
                break;
                
            case 5: // Fillers with 2 state
                fillPayloadMacro(4, 0x02);
                incrementCdeaStateMacroAfterNPacketsMacro(3);
                break;
                
            case 6: // Fillers with 1 state
                fillPayloadMacro(6, 0x01);
                incrementCdeaStateMacroAfterNPacketsMacro(3);
                break;
                
            case 7: // Fillers with 3 state
                fillPayloadMacro(8, 0x03);
                incrementCdeaStateMacroAfterNPacketsMacro(21);
                break;
                
            case 8: // Packet before stuff with MAC address state
                memset(payload, 0, PAYLOAD_SIZE);
                payload[2]  = 0x03;
                payload[12] = 0x03;
                payload[24] = 0xc0;
                payload[25] = 0xa8;
                
                incrementCdeaStateMacro();
                
                break;
                
            case 9: // Stuff with MAC address state
                memcpy(payload+PAYLOAD_SIZE-sizeof(stuffWithMACAddress[0]), 
                       stuffWithMACAddress[cdeaPacketsSinceStateChange],
                       sizeof(stuffWithMACAddress[0]));
                
                
                if (0 == cdeaPacketsSinceStateChange) {
                    payload[0] = payload[1] = 0x01;
                    connection->getInterfaceAddr(ETHER_ADDR_LEN, payload+2);
                    connection->getInterfaceAddr(ETHER_ADDR_LEN, payload+12);
                }
                else if (1 == cdeaPacketsSinceStateChange) {
                    payload[0] = payload[1] = 0xff;
                    memset(payload+2, 0, ETHER_ADDR_LEN);
                }
                else { // 3 == cdeaPacketsSinceStateChange
                    payload[0] = payload[1] = 0x00;
                    memset(payload+2, 0, ETHER_ADDR_LEN);
                }
                
                resetCdeaStateMacroAfterNPacketsMacro(3);
                
                break;
                
            default: // Shouldn't happen. Reset.
                setCdeaStateMacro(0);
                packetsUntilNextCdea = 0;
                cdeaAtChannel = 0;
                break;
        }
        
        setPacketTypeMacro(REAC_STREAM_CONTROL);
        memcpy(packet->data, REAC_STREAM_CONTROL_PACKET_TYPE[cdeaPacketType], sizeof(REAC_STREAM_CONTROL_PACKET_TYPE[cdeaPacketType]));
        
        applyChecksumAndSaveLastChecksumMacro();
    }
    else {
        setPacketTypeMacro(REAC_STREAM_FILLER);
        for (int i=0; i<31; i+=2) {
            packet->data[i+0] = lastCdeaTwoBytes[0];
            packet->data[i+1] = lastCdeaTwoBytes[1];
        }
    }
    
    --packetsUntilNextCdea;
    
    return kIOReturnSuccess;
}

bool REACMasterDataStream::gotPacket(const REACPacketHeader *packet, const EthernetHeader *header) {
    if (super::gotPacket(packet, header)) {
        return true;
    }
    
    if (isPacketType(packet, REAC_STREAM_SPLIT_ANNOUNCE)) {
        bool found = REACMasterDataStream::updateLastHeardFromSplitUnit(header, sizeof(header->shost), header->shost);
        if (!found && GOT_SPLIT_NOT_INITIATED == masterGotSplitAnnounceState) {
            masterGotSplitAnnounceState = GOT_SPLIT_ANNOUNCE;
            memcpy(masterSplitAnnounceAddr, packet->data+9 /* sorry about the magic constant */, sizeof(masterSplitAnnounceAddr));
        }
        return true;
    }
    else if (isControlPacketType(packet, CONTROL_PACKET_TYPE_SLAVE_ANNOUNCE2) ||
             isControlPacketType(packet, CONTROL_PACKET_TYPE_SLAVE_ANNOUNCE3) &&
             !gotSlaveAnnounce) {
        gotSlaveAnnounce = true;
        memcpy(slaveAnnounceData, packet->data, sizeof(slaveAnnounceData));
    }
    
    return false;
}

// TODO This function's implementation is not complete
bool REACMasterDataStream::isConnectedToSlave() const {
    return SLAVE_CONNECTION_GOT_SLAVE_ANNOUNCE == slaveConnectionStatus;
}

bool REACMasterDataStream::updateLastHeardFromSplitUnit(const EthernetHeader* header, UInt32 addrLen, const UInt8 *addr) {
    const UInt32   arraySize = splitUnits->getCount();
    bool           found = false;
    
    if (addrLen != sizeof(header->shost)) {
        return false;
    }
    
    for (UInt32 i=0; i<arraySize; i++) {
        REACSplitUnit *splitUnit = (REACSplitUnit *)splitUnits->getObject(i);
        if (0 == memcmp(addr, splitUnit->address, addrLen)) {
            found = true;
            splitUnit->lastHeardFrom = counter;
            break;
        }
    }
    
    return found;
}

IOReturn REACMasterDataStream::splitUnitConnected(UInt8 identifier, UInt32 addrLen, const UInt8 *addr) {
    IOReturn ret = kIOReturnError;
    REACSplitUnit *rsu = REACSplitUnit::withAddress(counter, identifier, addrLen, addr);
    
    if (NULL == rsu) {
        goto Done;
    }
    if (!splitUnits->setObject(rsu)) {
        goto Done;
    }
    
    IOLog("REACDataStream::splitUnitConnected(): Split connect: ");
    for (UInt32 i=0; i<addrLen; i++) IOLog("%02x", addr[i]);
    IOLog("\n");
    
    ret = kIOReturnSuccess;
    
Done:
    if (NULL != rsu) {
        rsu->release();
    }
    return ret;
}

void REACMasterDataStream::disconnectObsoleteSplitUnits() {
    UInt32 arraySize = splitUnits->getCount();
    
    for (UInt32 i=0; i<arraySize; i++) {
        REACSplitUnit *splitUnit = (REACSplitUnit *)splitUnits->getObject(i);
        if (counter-splitUnit->lastHeardFrom >= REAC_PACKETS_PER_SECOND) {
            IOLog("REACDataStream::disconnectObsoleteSplitUnits(): Split disconnect: ");
            for (UInt32 j=0; j<sizeof(splitUnit->address); j++) IOLog("%02x", splitUnit->address[j]);
            IOLog("\n");
            
            splitUnits->removeObject(i);
            --i;
            --arraySize;
        }
    }
}
