/*
 *  REACDataSteam.cpp
 *  REAC
 *
 *  Created by Per Eckerdal on 15/03/2011.
 *  Copyright 2011 Per Eckerdal. All rights reserved.
 *
 */

#include "REACDataStream.h"

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


const UInt8 REACDataStream::REAC_SPLIT_ANNOUNCE_FIRST[] = {
    0x01, 0x00, 0x7f, 0x00, 0x01, 0x03, 0x08, 0x43, 0x05
};

const UInt8 REACDataStream::STREAM_TYPE_IDENTIFIERS[][2] = {
    { 0x00, 0x00 }, // REAC_STREAM_FILLER
    { 0xcd, 0xea }, // REAC_STREAM_CONTROL
    { 0xcf, 0xea }, // REAC_STREAM_MASTER_ANNOUNCE
    { 0xce, 0xea }  // REAC_STREAM_SPLIT_ANNOUNCE
};


#define super OSObject

OSDefineMetaClassAndStructors(REACDataStream, OSObject)

bool REACDataStream::initConnection(REACConnection* conn) {
    connection = conn;
    lastAnnouncePacket = 0;
    counter = 0;
    recievedPacketCounter = 0;
    
    splitHandshakeState = HANDSHAKE_NOT_INITIATED;
    
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
    
    return true;
    
Fail:
    deinit();
    return false;
}

REACDataStream *REACDataStream::withConnection(REACConnection *conn) {
    REACDataStream *s = new REACDataStream;
    if (NULL == s) return NULL;
    bool result = s->initConnection(conn);
    if (!result) {
        s->release();
        return NULL;
    }
    return s;
}

void REACDataStream::deinit() {
    if (NULL != splitUnits) {
        splitUnits->release();
        splitUnits = NULL;
    }
}

void REACDataStream::free() {
    deinit();
    super::free();
}

void REACDataStream::gotPacket(const REACPacketHeader *packet, const EthernetHeader *header) {
    recievedPacketCounter++;
    
    if (0 == memcmp(packet->type,
                    STREAM_TYPE_IDENTIFIERS[REAC_STREAM_FILLER],
                    sizeof(STREAM_TYPE_IDENTIFIERS[0]))) {
        return;
    }
    
    if (!REACDataStream::checkChecksum(packet)) {
        IOLog("REACDataStream::gotPacket(): Got packet with invalid checksum.\n");
    }
    
    /*IOLog("Got packet: "); // TODO Debug
    for (UInt32 i=0; i<sizeof(REACPacketHeader); i++) {
        IOLog("%02x", ((UInt8*)packet)[i]);
    }
    IOLog("\n");*/
    
    if (0 == memcmp(packet->type,
                    STREAM_TYPE_IDENTIFIERS[REAC_STREAM_SPLIT_ANNOUNCE],
                    sizeof(STREAM_TYPE_IDENTIFIERS[0]))) {
        if (REACConnection::REAC_MASTER == connection->getMode()) {
            bool found = REACDataStream::updateLastHeardFromSplitUnit(header, sizeof(header->shost), header->shost);
            if (!found && GOT_SPLIT_NOT_INITIATED == masterGotSplitAnnounceState) {
                masterGotSplitAnnounceState = GOT_SPLIT_ANNOUNCE;
                memcpy(masterSplitAnnounceAddr, packet->data+sizeof(REAC_SPLIT_ANNOUNCE_FIRST), sizeof(masterSplitAnnounceAddr));
            }
        }
    }
    if (0 == memcmp(packet->type,
                    STREAM_TYPE_IDENTIFIERS[REAC_STREAM_MASTER_ANNOUNCE],
                    sizeof(STREAM_TYPE_IDENTIFIERS[0]))) {
        if (REACConnection::REAC_SPLIT) {
            MasterAnnouncePacket *map = (MasterAnnouncePacket *)packet->data;
            if (HANDSHAKE_NOT_INITIATED == splitHandshakeState) {
                if (0x0d == map->unknown1[6]) {
                    memcpy(splitMasterDevice.addr, map->address, sizeof(splitMasterDevice.addr));
                    splitMasterDevice.in_channels = map->inChannels;
                    splitMasterDevice.out_channels = map->outChannels;
                    splitHandshakeState = HANDSHAKE_GOT_MASTER_ANNOUNCE;
                }
            }
            else if (HANDSHAKE_SENT_FIRST_ANNOUNCE == splitHandshakeState) {
                if (0x0a == map->unknown1[6]) {
                    if (0 == connection->interfaceAddrCmp(sizeof(map->address), map->address)) {
                        splitIdentifier = map->outChannels;
                        splitHandshakeState = HANDSHAKE_GOT_SECOND_MASTER_ANNOUNCE;
                    }
                }
            }
        }
    }
}

IOReturn REACDataStream::processPacket(REACPacketHeader *packet, UInt32 dhostLen, UInt8 *dhost) {
    
#   define setPacketTypeMacro(packetType) \
        memcpy(packet->type, STREAM_TYPE_IDENTIFIERS[packetType], sizeof(packet->type));
    
    packet->setCounter(counter++);
    
    memset(dhost, 0xff, dhostLen);
    
    if (REACConnection::REAC_MASTER == connection->getMode() && GOT_SPLIT_ANNOUNCE == masterGotSplitAnnounceState) {
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
        
        static const SInt32 CDEA_PACKET_TYPE_SIZE = 5;
        static const SInt32 PAYLOAD_SIZE = sizeof(packet->data)-CDEA_PACKET_TYPE_SIZE-1; // -2 for checksum
        UInt8 *payload = packet->data+CDEA_PACKET_TYPE_SIZE;
        
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
        
        static const UInt8 cdeaPacketTypes[][CDEA_PACKET_TYPE_SIZE] = {
            { 0x01, 0x00, 0x00, 0x1a, 0x00 },
            { 0x01, 0x02, 0x00, 0x0e, 0x00 },
            { 0x01, 0x03, 0x00, 0x19, 0x01 },
            { 0x01, 0x01, 0x00, 0x18, 0x00 }
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
        memcpy(packet->data, cdeaPacketTypes[cdeaPacketType], sizeof(cdeaPacketTypes[cdeaPacketType]));
        
        lastCdeaTwoBytes[0] = packet->data[sizeof(packet->data)-2];
        lastCdeaTwoBytes[1] = REACDataStream::applyChecksum(packet);
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

bool REACDataStream::prepareSplitAnnounce(REACPacketHeader *packet) {
    bool ret = false;
    
    memcpy(packet->type, STREAM_TYPE_IDENTIFIERS[REACDataStream::REAC_STREAM_SPLIT_ANNOUNCE], sizeof(packet->type));
    
    if (HANDSHAKE_GOT_MASTER_ANNOUNCE == splitHandshakeState) {
        memset(packet->data, 0, sizeof(packet->data));
        memcpy(packet->data, REAC_SPLIT_ANNOUNCE_FIRST, sizeof(REAC_SPLIT_ANNOUNCE_FIRST));
        connection->getInterfaceAddr(ETHER_ADDR_LEN, packet->data+sizeof(REAC_SPLIT_ANNOUNCE_FIRST));
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
        
        connection->getInterfaceAddr(ETHER_ADDR_LEN, packet->data+sizeof(REAC_SPLIT_ANNOUNCE_FIRST));
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

bool REACDataStream::updateLastHeardFromSplitUnit(const EthernetHeader* header, UInt32 addrLen, const UInt8 *addr) {
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

IOReturn REACDataStream::splitUnitConnected(UInt8 identifier, UInt32 addrLen, const UInt8 *addr) {
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

void REACDataStream::disconnectObsoleteSplitUnits() {
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
