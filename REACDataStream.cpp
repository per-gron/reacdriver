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
    lastChecksum = 0;
    
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
    else if (false) {
        lastChecksum = REACDataStream::applyChecksum(packet);
    }
    else {
        memcpy(packet->type, REACDataStream::STREAM_TYPE_IDENTIFIERS[REAC_STREAM_FILLER], sizeof(packet->type));
        for (int i=0; i<31; i+=2) {
            packet->data[i] = 0;
            packet->data[i+1] = lastChecksum;
        }
    }
    
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
