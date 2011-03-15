/*
 *  REACDataStream.h
 *  REAC
 *
 *  Created by Per Eckerdal on 15/03/2011.
 *  Copyright 2011 Per Eckerdal. All rights reserved.
 *
 */

#ifndef _REACDATASTREAM_H
#define _REACDATASTREAM_H

#include <libkern/OSTypes.h>
#include <libkern/c++/OSObject.h>
#include <IOKit/IOReturn.h>

#define REACPacketHeader        com_pereckerdal_driver_REACPacketHeader
#define REACDataStream          com_pereckerdal_driver_REACDataStream


/* REAC packet header */
struct REACPacketHeader {
    UInt8 counter[2];
    UInt8 type[2];
    UInt8 data[32];
    
    UInt16 getCounter() {
        UInt16 ret = counter[0];
        ret += ((UInt16) counter[1]) << 8;
        return ret;
    }
    void setCounter(UInt16 c) {
        counter[0] = c;
        counter[1] = c >> 8;
    }
};

// Handles the data stream part of a REAC stream (both input and output).
// Each REAC connection is supposed to have one of these objects.
//
// This class is not thread safe.
//
// TODO Private constructor/assignment operator/destructor?
class REACDataStream : public OSObject {
    OSDeclareDefaultStructors(REACDataStream)
public:
    enum REACStreamType {
        REAC_STREAM_FILLER = 0,
        REAC_STREAM_CONTROL = 1,
        REAC_STREAM_CONTROL2 = 2,
        REAC_STREAM_FROM_SPLIT = 3
    };
    
    static const UInt8 STREAM_TYPE_IDENTIFIERS[][2];
    
    virtual bool init();
    static REACDataStream *with();
protected:
    // Object destruction method that is used by free, and init on failure.
    virtual void deinit();
    virtual void free();
    
public:
    
    void gotPacket(const REACPacketHeader *packet);
    IOReturn processPacket(REACPacketHeader *packet);
    
    static bool checkChecksum(const REACPacketHeader *packet);
    static UInt8 applyChecksum(REACPacketHeader *packet);
    
protected:
    
    UInt16 counter;
    UInt8  lastChecksum;
};


#endif
