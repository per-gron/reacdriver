/*
 *  REACConnection.h
 *  REAC
 *
 *  Created by Per Eckerdal on 02/01/2011.
 *  Copyright 2011 Per Eckerdal. All rights reserved.
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

#define EthernetHeader              com_pereckerdal_driver_EthernetHeader
#define REACDeviceInfo              com_pereckerdal_driver_REACDeviceInfo
#define REACConnection              com_pereckerdal_driver_REACConnection


/* Ethernet header */
struct EthernetHeader {
	UInt8 dhost[ETHER_ADDR_LEN]; /* Destination host address */
	UInt8 shost[ETHER_ADDR_LEN]; /* Source host address */
	UInt16 type; /* IP? ARP? RARP? etc */
};

struct REACDeviceInfo {
    UInt8 addr[ETHER_ADDR_LEN];
    UInt32 in_channels;
    UInt32 out_channels;
};

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
    
    static IOReturn getInterfaceMacAddress(ifnet_t interface, UInt8* addr, UInt32 addrLen);
public:
    enum REACMode {
        REAC_MASTER, REAC_SLAVE, REAC_SPLIT
    };
    
    virtual bool initWithInterface(IOWorkLoop *workLoop, ifnet_t interface, REACMode mode,
                                   reac_connection_callback_t connectionCallback,
                                   reac_samples_callback_t samplesCallback,
                                   reac_get_samples_callback_t getSamplesCallback,
                                   void *cookieA,
                                   void *cookieB);
    static REACConnection *withInterface(IOWorkLoop *workLoop, ifnet_t interface, REACMode mode,
                                         reac_connection_callback_t connectionCallback,
                                         reac_samples_callback_t samplesCallback,
                                         reac_get_samples_callback_t getSamplesCallback,
                                         void *cookieA,
                                         void *cookieB);
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
    
    // Connection state variables
    REACMode            mode;
    bool                started;
    bool                connected;
    REACDataStream     *dataStream;
    REACDeviceInfo     *deviceInfo;
    UInt16              lastCounter; // Tracks input REAC counter
    
    static void timerFired(OSObject *target, IOTimerEventSource *sender);
    
    IOReturn getAndPushSamples();
    // When sampleBuffer is NULL, the sample data will be zeros (and bufSize will be disregarded).
    IOReturn pushSamples(UInt32 bufSize, UInt8 *sampleBuffer);
    
    static void filterCommandGateMsg(OSObject *target, void *data_mbuf, void*, void*, void*);
    
    static errno_t filterInputFunc(void *cookie,
                                   ifnet_t interface, 
                                   protocol_family_t protocol,
                                   mbuf_t *data,
                                   char **frame_ptr);        
    static void filterDetachedFunc(void *cookie,
                                   ifnet_t interface);
    
};


#endif
