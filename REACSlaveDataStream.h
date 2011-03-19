/*
 *  REACSlaveDataStream.h
 *  REAC
 *
 *  Created by Per Eckerdal on 18/03/2011.
 *  Copyright 2011 Per Eckerdal. All rights reserved.
 *
 */

#ifndef _REACSLAVEDATASTREAM_H
#define _REACSLAVEDATASTREAM_H

#include "REACDataStream.h"
#include "EthernetHeader.h"

#define REACSlaveDataStream    com_pereckerdal_driver_REACSlaveDataStream

class REACSlaveDataStream : public REACDataStream {
    OSDeclareDefaultStructors(REACSlaveDataStream)
    
protected:
    
    virtual bool initConnection(com_pereckerdal_driver_REACConnection *conn);
    
public:
    
    virtual IOReturn processPacket(REACPacketHeader *packet, UInt32 dhostLen, UInt8 *dhost);
    virtual bool gotPacket(const REACPacketHeader *packet, const EthernetHeader *header);
    
protected:
    enum HandshakeState {
        HANDSHAKE_NOT_INITIATED,
        HANDSHAKE_GOT_MAC_ADDRESS_INFO, // We need to be careful here so that we never get stuck. lastGotMacAddressInfo is to stop that.
        HANDSHAKE_SENDING_INITIAL_ANNOUNCE,
        HANDSHAKE_HAS_SENT_ANNOUNCE,
        HANDSHAKE_CONNECTED
    };
    HandshakeState      handshakeState;
    UInt32              handshakeSubState;
    UInt64              lastGotMacAddressInfoStateUpdate;
    SInt32              firstChannelInfoId; // Is -1 before we got anything
    REACDeviceInfo      masterDevice;
    UInt8               lastCdeaTwoBytes[2];
    
    void setHandshakeState(HandshakeState state);
    void resetHandshakeState();
};

#endif
