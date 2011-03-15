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

#define super OSObject

OSDefineMetaClassAndStructors(REACDataStream, OSObject)

bool REACDataStream::init() {
    if (false) goto Fail; // Supress the unused label warning
    
    return true;
    
Fail:
    deinit();
    return false;
}

REACDataStream *REACDataStream::with() {
    REACDataStream *s = new REACDataStream;
    if (NULL == s) return NULL;
    bool result = s->init();
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
    UInt16 fill;
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
            
    }
}

void REACDataStream::processPacket(REACPacketHeader *packet) {
    REACDataStream::applyChecksum(packet);
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
