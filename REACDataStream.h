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
#include <libkern/c++/OSArray.h>
#include <IOKit/IOReturn.h>

#include "REACConstants.h"
#include "EthernetHeader.h"

#define REACPacketHeader        com_pereckerdal_driver_REACPacketHeader
#define REACDataStream          com_pereckerdal_driver_REACDataStream
#define REACDeviceInfo          com_pereckerdal_driver_REACDeviceInfo

class com_pereckerdal_driver_REACConnection;

struct REACDeviceInfo {
    UInt8 addr[ETHER_ADDR_LEN];
    UInt32 in_channels;
    UInt32 out_channels;
};

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
// This is an abstract class and is supposed to be inherited to implement
// the characteristics of the different REAC modes. It does, however,
// provide a factory method for creating REACDataStream* objects.
//
// TODO Protected constructor/assignment operator/destructor?
class REACDataStream : public OSObject {
    OSDeclareDefaultStructors(REACDataStream)
    
    struct MasterAnnouncePacket {
        UInt8 unknown1[9];
        UInt8 address[ETHER_ADDR_LEN];
        UInt8 inChannels;
        UInt8 outChannels;
        UInt8 unknown2[4];
    };
    
    enum REACStreamType {
        REAC_STREAM_FILLER = 0,
        REAC_STREAM_CONTROL = 1,
        REAC_STREAM_MASTER_ANNOUNCE = 2,
        REAC_STREAM_SPLIT_ANNOUNCE = 3
    };
    
    static const UInt8 STREAM_TYPE_IDENTIFIERS[][2];
    
    enum REACStreamControlPacketType {
        CONTROL_PACKET_TYPE_ONE = 0,
        CONTROL_PACKET_TYPE_TWO = 1,
        CONTROL_PACKET_TYPE_THREE = 2,
        CONTROL_PACKET_TYPE_FOUR = 3,
        CONTROL_PACKET_TYPE_SLAVE_ANNOUNCE1 = 4,
        CONTROL_PACKET_TYPE_SLAVE_ANNOUNCE2 = 5,
        CONTROL_PACKET_TYPE_SLAVE_ANNOUNCE3 = 6,
        CONTROL_PACKET_TYPE_SLAVE_ANNOUNCE4 = 7
    };
#   define REAC_STREAM_CONTROL_PACKET_TYPE_SIZE 5
    static const UInt8 REAC_STREAM_CONTROL_PACKET_TYPE[][REAC_STREAM_CONTROL_PACKET_TYPE_SIZE];
    
    virtual bool initConnection(com_pereckerdal_driver_REACConnection *conn);
    
public:
    
    static REACDataStream *withConnection(com_pereckerdal_driver_REACConnection *conn);
    
public:

    // Return kIOReturnSuccess on success, kIOReturnAborted if no packet should be
    // sent, and anything else on error.
    //
    // This method will return kIOReturnAborted. Classes inheriting this class
    // should overload this method if they wish to do anything else. If this method
    // is overloaded, it must call this method.
    virtual IOReturn processPacket(REACPacketHeader *packet, UInt32 dhostLen, UInt8 *dhost);
    
    // Returns true if the processing is finished and no further processing should
    // be done.
    //
    // If this method is overloaded, it should be called before any other processing.
    // If it returns true, the overloaded method must not do any processing and must
    // return true.
    virtual bool gotPacket(const REACPacketHeader *packet, const EthernetHeader *header);
    
protected:
    
    com_pereckerdal_driver_REACConnection *connection;
    UInt64    lastAnnouncePacket; // The counter of the last announce counter packet
    UInt64    recievedPacketCounter;
    UInt64    counter;
        
    static bool checkChecksum(const REACPacketHeader *packet);
    static UInt8 applyChecksum(REACPacketHeader *packet);
    
    static bool isPacketType(const REACPacketHeader *packet, REACStreamType st);
    static bool isControlPacketType(const REACPacketHeader *packet, REACStreamControlPacketType type);
};


#endif
