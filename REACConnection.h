/*
 *  REACConnection.h
 *  REAC
 *
 *  Created by Per Eckerdal on 02/01/2011.
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

#ifndef _REACCONNECTION_H
#define _REACCONNECTION_H

#include <IOKit/audio/IOAudioDevice.h>
#include <IOKit/IOCommandGate.h>
#include <net/kpi_interface.h>
#include <sys/kpi_mbuf.h>
#include <net/kpi_interfacefilter.h>

#include "REACDataStream.h"
#include "REACConstants.h"
#include "EthernetHeader.h"

#define REACConnection              com_pereckerdal_driver_REACConnection

class REACConnection;

// Device is NULL on disconnect
typedef void(*reac_connection_callback_t)(REACConnection *proto, void **cookieA, void **cookieB, REACDeviceInfo *device);
// Is only called when the connection callback has indicated that there is a connection
typedef void(*reac_samples_callback_t)(REACConnection *proto, void **cookieA, void **cookieB, UInt8 **data, UInt32 *bufferSize);
// Is only called when in REAC_MASTER or REAC_SLAVE mode and the connection callback has
// indicated that there is a connection.
typedef void(*reac_get_samples_callback_t)(REACConnection *proto, void **cookieA, void **cookieB, UInt8 **data, UInt32 *bufferSize);


// This class is not thread safe; the only functions that can be called
// without being synchronized to the work loop are the interface filter
// callbacks. The samplesCallback and connectionCallback callbacks are
// guaranteed to be called from within the work loop.
//
// TODO Private constructor/assignment operator/destructor?
class REACConnection : public OSObject {
    OSDeclareDefaultStructors(REACConnection)
    
public:
    enum REACMode {
        REAC_MASTER, REAC_SLAVE, REAC_SPLIT
    };
    
    virtual bool initWithInterface(IOWorkLoop *workLoop, ifnet_t interface, REACMode mode,
                                   reac_connection_callback_t connectionCallback,
                                   reac_samples_callback_t samplesCallback,
                                   reac_get_samples_callback_t getSamplesCallback,
                                   void *cookieA,
                                   void *cookieB,
                                   UInt8 inChannels = 0, // Only used in REAC_MASTER mode
                                   UInt8 outChannels = 0); // Only used in REAC_MASTER mode
    static REACConnection *withInterface(IOWorkLoop *workLoop, ifnet_t interface, REACMode mode,
                                         reac_connection_callback_t connectionCallback,
                                         reac_samples_callback_t samplesCallback,
                                         reac_get_samples_callback_t getSamplesCallback,
                                         void *cookieA,
                                         void *cookieB,
                                         UInt8 inChannels = 0, // Only used in REAC_MASTER mode
                                         UInt8 outChannels = 0); // Only used in REAC_MASTER mode
protected:
    // Object destruction method that is used by free, and initWithInterface on failure.
    virtual void deinit();
    virtual void free();
public:
    bool start();
    void stop();
    
    const REACDeviceInfo *getDeviceInfo() const;
    bool isStarted() const { return started; }
    bool isConnected() const { return connected; }
    // If you want to continue using the ifnet_t object, make sure to call
    // ifnet_reference on it, as REACConnection will release it when it is freed.
    ifnet_t getInterface() const { return interface; }
    REACMode getMode() const { return mode; }
    IOReturn getInterfaceAddr(UInt32 len, UInt8 *addr) const {
        if (sizeof(interfaceAddr) != len) return kIOReturnBadArgument;
        memcpy(addr, interfaceAddr, len);
        return kIOReturnSuccess;
    }
    int interfaceAddrCmp(UInt32 len, UInt8 *addr) const {
        if (sizeof(interfaceAddr) != len) return 1;
        return memcmp(addr, interfaceAddr, len);
    }
    UInt8 getInChannels() const { return inChannels; }
    UInt8 getOutChannels() const { return outChannels; }

protected:
    // IOKit handles
    IOWorkLoop         *workLoop;
    IOTimerEventSource *timerEventSource;        // Note that the timer runs faster when in REAC_MASTER mode than otherwise
    IOCommandGate      *filterCommandGate;
    UInt64              timeoutNS;
    UInt64              nextTime;                // the estimated time the timer will fire next
    
    // Network handles
    UInt8               interfaceAddr[ETHER_ADDR_LEN];
    ifnet_t             interface;
    interface_filter_t  filterRef;
    
    // Callback variables
    reac_connection_callback_t  connectionCallback;
    reac_samples_callback_t     samplesCallback;
    reac_get_samples_callback_t getSamplesCallback;
    void *cookieA;
    void *cookieB;
    
    // Variables for keeping track of when a connection is lost
    UInt64              lastSeenConnectionCounter;
    UInt64              connectionCounter;
    UInt64              lastSentAnnouncementCounter;
    UInt16              splitAnnouncementCounter;
    
    // Connection state variables
    REACMode            mode;
    UInt8               inChannels;  // The number of input channels (seen as outputs in the computer) Only used in REAC_MASTER mode
    UInt8               outChannels; // The number of output channels (seen as inputs in the computer) Only used in REAC_MASTER mode
    bool                started;
    bool                connected;
    REACDataStream     *dataStream;
    REACDeviceInfo     *deviceInfo;
    UInt16              lastCounter; // Tracks input REAC counter
    
    static void timerFired(OSObject *target, IOTimerEventSource *sender);
    
    IOReturn getAndSendSamples();
    // When sampleBuffer is NULL, the sample data will be zeros (and bufSize will be disregarded).
    IOReturn sendSamples(UInt32 bufSize, UInt8 *sampleBuffer);
    IOReturn sendSplitAnnouncementPacket();
    
    static void filterCommandGateMsg(OSObject *target, void *data_mbuf, void *eth_header_ptr, void*, void*);
    
    static errno_t filterInputFunc(void *cookie,
                                   ifnet_t interface, 
                                   protocol_family_t protocol,
                                   mbuf_t *data,
                                   char **frame_ptr);        
    static void filterDetachedFunc(void *cookie,
                                   ifnet_t interface);
    static IOReturn getInterfaceMacAddress(ifnet_t interface, UInt8* addr, UInt32 addrLen);
    
};


#endif
