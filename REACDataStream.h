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

#define REACPacketHeader        com_pereckerdal_driver_REACPacketHeader
#define REACDataStream          com_pereckerdal_driver_REACDataStream


/* REAC packet header */
struct REACPacketHeader {
    UInt16 counter;
    UInt16 type;
    UInt8  data[32];
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
    enum REACStreamType { // TODO Endianness?
        REAC_STREAM_FILLER = 0,
        REAC_STREAM_CONTROL = 0xeacd,
        REAC_STREAM_CONTROL2 = 0xeacf,
        REAC_STREAM_FROM_SPLIT = 0xeace
    };
    
    virtual bool init();
    static REACDataStream *with();
protected:
    // Object destruction method that is used by free, and init on failure.
    virtual void deinit();
    virtual void free();
    
public:
    
    void gotPacket(const REACPacketHeader *packet);
    void processPacket(REACPacketHeader *packet);
    
    static bool checkChecksum(const REACPacketHeader *packet);
    static UInt8 applyChecksum(REACPacketHeader *packet);
    
protected:

};


#endif
