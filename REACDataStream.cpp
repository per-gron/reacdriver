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

const UInt8 REACDataStream::STREAM_TYPE_IDENTIFIERS[][2] = {
    { 0x00, 0x00 }, // REAC_STREAM_FILLER
    { 0xcd, 0xea }, // REAC_STREAM_CONTROL
    { 0xcf, 0xea }, // REAC_STREAM_MASTER_ANNOUNCE
    { 0xce, 0xea }  // REAC_STREAM_SPLIT_ANNOUNCE
};


#define super OSObject

OSDefineMetaClassAndStructors(REACDataStream, OSObject)

bool REACDataStream::initConnection(REACConnection* conn) {
    if (false) goto Fail; // Supress the unused label warning
    
    connection = conn;
    lastAnnouncePacket = 0;
    counter = 0;
    lastCdeaTwoBytes[0] = lastCdeaTwoBytes[1] = 0;
    
    packetsUntilNextCdea = 0;
    cdeaState = 0;
    cdeaPacketsSinceStateChange = -1;
    cdeaAtChannel = 0;
    
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
}

void REACDataStream::free() {
    deinit();
    super::free();
}

void REACDataStream::gotPacket(const REACPacketHeader *packet) {
    /*UInt16 fill;
    UInt16 *data = (UInt16 *)packet->data;
    
    switch (packet->type) {
        case REAC_STREAM_FILLER:
            fill = data[0];
            for (int i=1; i<16; i++) {
                if (data[i] != fill) {
                    IOLog("REACConnection[%p]::processDataStream(): Unexpected type 0 packet!\n", this);
                    break;
                }
            }
            
            break;
            
        case REAC_STREAM_CONTROL:
        case REAC_STREAM_CONTROL2:
            // IOLog("REACConnection[%p]::processDataStream(): Got control [%d]\n", this, packet->counter);
            break;
            
    }*/
}

IOReturn REACDataStream::processPacket(REACPacketHeader *packet) {
    packet->setCounter(counter++);
    
    if (counter-lastAnnouncePacket >= REAC_PACKETS_PER_SECOND) {
        lastAnnouncePacket = counter;
        
        memcpy(packet->type, REACDataStream::STREAM_TYPE_IDENTIFIERS[REAC_STREAM_MASTER_ANNOUNCE], sizeof(packet->type));
        
        AnnouncePacket *ap = (AnnouncePacket *)packet->data;
        ap->unknown1[0] = 0xff;
        ap->unknown1[1] = 0xff;
        ap->unknown1[2] = 0x01;
        ap->unknown1[3] = 0x00;
        ap->unknown1[4] = 0x01;
        ap->unknown1[5] = 0x03;
        ap->unknown1[6] = 0x0d;
        ap->unknown1[7] = 0x01;
        ap->unknown1[8] = 0x04;
        
        connection->getInterfaceAddr(sizeof(ap->address), ap->address);
        
        ap->inChannels = connection->getInChannels();
        ap->outChannels = connection->getOutChannels();
                                     
        ap->unknown2[0] = 0x01;
        ap->unknown2[1] = 0x00;
        ap->unknown2[2] = 0x01;
        ap->unknown2[3] = 0x00;
        
        memset(packet->data+sizeof(AnnouncePacket), 0, sizeof(packet->data)-sizeof(AnnouncePacket));
        
        REACDataStream::applyChecksum(packet);
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
        
        memcpy(packet->type, REACDataStream::STREAM_TYPE_IDENTIFIERS[REAC_STREAM_CONTROL], sizeof(packet->type));
        memcpy(packet->data, cdeaPacketTypes[cdeaPacketType], sizeof(cdeaPacketTypes[cdeaPacketType]));
        
        lastCdeaTwoBytes[0] = packet->data[sizeof(packet->data)-2];
        lastCdeaTwoBytes[1] = REACDataStream::applyChecksum(packet);
    }
    else {
        memcpy(packet->type, REACDataStream::STREAM_TYPE_IDENTIFIERS[REAC_STREAM_FILLER], sizeof(packet->type));
        for (int i=0; i<31; i+=2) {
            packet->data[i+0] = lastCdeaTwoBytes[0];
            packet->data[i+1] = lastCdeaTwoBytes[1];
        }
    }
    
    --packetsUntilNextCdea;
    
    return kIOReturnSuccess;
}

bool REACDataStream::checkChecksum(const REACPacketHeader *packet) {
    UInt8 expected_checksum = 0;
    for (int i=0; i<31; i++) {
        if (i%2)
            expected_checksum += (UInt8) (packet->data[i/2] >> 8);
        else
            expected_checksum += (UInt8) packet->data[i/2] & 255;
    }
    expected_checksum = (UInt8) (256 - (int)expected_checksum);
    
    return expected_checksum == packet->data[15] >> 8;
}

UInt8 REACDataStream::applyChecksum(REACPacketHeader *packet) {
    UInt8 sum = 0;
    for (int i=0; i<31; i++)
        sum += packet->data[i];
    sum = (256 - (int)sum);
    packet->data[31] = sum;
    return sum;
}
