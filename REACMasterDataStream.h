/*
 *  REACMasterDataStream.h
 *  REAC
 *
 *  Created by Per Eckerdal on 18/03/2011.
 *  Copyright 2011 Per Eckerdal. All rights reserved.
 *
 */

#ifndef _REACMASTERDATASTREAM_H
#define _REACMASTERDATASTREAM_H

#include "REACDataStream.h"
#include "EthernetHeader.h"

#define REACSplitUnit           com_pereckerdal_driver_REACSplitUnit
#define REACMasterDataStream    com_pereckerdal_driver_REACMasterDataStream

// Represents one connected REAC_SPLIT device
class REACSplitUnit : public OSObject {
    OSDeclareFinalStructors(REACSplitUnit);
    
public:
    virtual bool initAddress(UInt64 lastHeardFrom, UInt8 identifier, UInt32 addrLen, const UInt8 *addr);
    static REACSplitUnit *withAddress(UInt64 lastHeardFrom, UInt8 identifier, UInt32 addrLen, const UInt8 *addr);
    
    UInt64 lastHeardFrom;
    UInt8  identifier;
    UInt8  address[ETHER_ADDR_LEN];
};

class REACMasterDataStream : public REACDataStream {
    OSDeclareDefaultStructors(REACMasterDataStream)
    
protected:
    struct SplitAnnounceResponsePacket {
        UInt8 unknown1[9];
        UInt8 address[ETHER_ADDR_LEN];
        UInt8 unknown2;
        UInt8 identifierAssignment;
    };

    virtual bool initConnection(com_pereckerdal_driver_REACConnection *conn);
    
    // Object destruction method that is used by free, and init on failure.
    virtual void deinit();
    virtual void free();
    
public:
    
    virtual IOReturn processPacket(REACPacketHeader *packet, UInt32 dhostLen, UInt8 *dhost);
    virtual bool gotPacket(const REACPacketHeader *packet, const EthernetHeader *header);
    
    bool isConnectedToSlave() const;
    
protected:
    enum GotSplitAnnounceState {
        GOT_SPLIT_NOT_INITIATED,
        GOT_SPLIT_ANNOUNCE,
        GOT_SPLIT_SENT_SPLIT_ANNOUNCE_RESPONSE
    };
    OSArray               *splitUnits;
    GotSplitAnnounceState  masterGotSplitAnnounceState;
    UInt8                  masterSplitAnnounceAddr[ETHER_ADDR_LEN];
    
    bool updateLastHeardFromSplitUnit(const EthernetHeader *header, UInt32 addrLen, const UInt8 *addr);
    IOReturn splitUnitConnected(UInt8 identifier, UInt32 addrLen, const UInt8 *addr);
    void disconnectObsoleteSplitUnits();
    
    // Cdea state
    UInt8     lastCdeaTwoBytes[2];
    SInt32    packetsUntilNextCdea;
    SInt32    cdeaState;
    SInt32    cdeaPacketsSinceStateChange;
    SInt32    cdeaAtChannel;     // Used when writing the cdea channel info packets
    SInt32    cdeaCurrentOffset; // Used when writing the cdea filler packets
    
    // Slave handshake state
    enum SlaveConnectionStatus {
        SLAVE_CONNECTION_NO_CONNECTION,
        SLAVE_CONNECTION_GOT_SLAVE_ANNOUNCE
    };
    SlaveConnectionStatus   slaveConnectionStatus;
    bool                    gotSlaveAnnounce;
    UInt8                   slaveAnnounceData[32];
};

#endif
