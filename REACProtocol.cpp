/*
 *  REACProtocol.cpp
 *  REAC
 *
 *  Created by Per Eckerdal on 02/01/2011.
 *  Copyright 2011 Per Eckerdal. All rights reserved.
 *
 */

#include "REACProtocol.h"

#include <IOKit/IOLib.h>
#include <IOKit/IOTimerEventSource.h>
#include <sys/errno.h>

#define REAC_CONNECTION_CHECK_TIMEOUT 400
#define REAC_TIMEOUT_UNTIL_DISCONNECT 1000

#define super OSObject

OSDefineMetaClassAndStructors(REACProtocol, OSObject)


bool REACProtocol::initWithInterface(IOWorkLoop *workLoop_, ifnet_t interface_, REACMode mode_,
                                     reac_connection_callback_t connectionCallback_,
                                     reac_samples_callback_t samplesCallback_,
                                     void* cookieA_,
                                     void* cookieB_) {
    if (NULL == workLoop_) {
        goto Fail;
    }
    workLoop = workLoop_;
    workLoop->retain();
    
    // Add the command gate to the workloop
    filterCommandGate = IOCommandGate::commandGate(this, (IOCommandGate::Action)filterCommandGateMsg);
    if (NULL == filterCommandGate ||
        (workLoop->addEventSource(filterCommandGate) != kIOReturnSuccess) ) {
        IOLog("REACProtocol::initWithInterface() - Error: Can't create or add commandGate\n");
        goto Fail;
    }
    
    // Add the timer event source to the workloop
    connectionCounter = 0;
    lastSeenConnectionCounter = 0;
    timerEventSource = IOTimerEventSource::timerEventSource(this, (IOTimerEventSource::Action)&REACProtocol::timerFired);
    if (NULL == timerEventSource) {
        IOLog("REACProtocol::initWithInterface() - Error: Failed to create timer event source.\n");
        goto Fail;
    }
    
    // Hack: Pretend to be connected immediately
    deviceInfo = (REACDeviceInfo*) IOMalloc(sizeof(REACDeviceInfo));
    if (NULL == deviceInfo) {
        goto Fail;
    }
    deviceInfo->addr[0] = 0x00;
    deviceInfo->addr[1] = 0x40;
    deviceInfo->addr[2] = 0xab;
    deviceInfo->addr[3] = 0xc4;
    deviceInfo->addr[4] = 0x80;
    deviceInfo->addr[5] = 0xf6;
    deviceInfo->in_channels = 16;
    deviceInfo->out_channels = 8;
    listening = false;
    connected = false;
    
    lastCounter = 0;
    samplesCallback = samplesCallback_;
    connectionCallback = connectionCallback_;
    cookieA = cookieA_;
    cookieB = cookieB_;
    mode = mode_;
    
    ifnet_reference(interface_);
    interface = interface_;
    
    return true;
    
Fail:
    deinit();
    return false;
}

REACProtocol* REACProtocol::withInterface(IOWorkLoop *workLoop, ifnet_t interface, REACMode mode,
                                          reac_connection_callback_t connectionCallback,
                                          reac_samples_callback_t samplesCallback,
                                          void* cookieA,
                                          void* cookieB) {
    REACProtocol* p = new REACProtocol;
    if (NULL == p) return NULL;
    bool result = p->initWithInterface(workLoop, interface, mode, connectionCallback, samplesCallback, cookieA, cookieB);
    if (!result) {
        p->release();
        return NULL;
    }
    return p;
}

void REACProtocol::deinit() {
    detach();
    
    if (NULL != deviceInfo) {
        IOFree(deviceInfo, sizeof(REACDeviceInfo));
    }
    
    if (NULL != filterCommandGate) {
        workLoop->removeEventSource(filterCommandGate);
        filterCommandGate->release();
        filterCommandGate = NULL;
    }
    
    if (NULL != workLoop) {
        workLoop->release();
    }
    
    if (NULL != timerEventSource) {
        timerEventSource->cancelTimeout();
        workLoop->removeEventSource(timerEventSource);
        timerEventSource->release();
        timerEventSource = NULL;
    }
    
    if (NULL != interface) {
        ifnet_release(interface);
        interface = NULL;
    }
}

void REACProtocol::free() {
    deinit();
    super::free();
}


bool REACProtocol::listen() {
    if (NULL == timerEventSource || workLoop->addEventSource(timerEventSource) != kIOReturnSuccess) {
        IOLog("REACProtocol::listen() - Error: Failed to add timer event source to work loop!\n");
        return false;
    }
    timerEventSource->setTimeoutMS(REAC_CONNECTION_CHECK_TIMEOUT);
    
    iff_filter filter;
    filter.iff_cookie = this;
    filter.iff_name = "REAC driver input filter";
    filter.iff_protocol = 0;
    filter.iff_input = &REACProtocol::filterInputFunc;
    filter.iff_output = NULL;
    filter.iff_event = NULL;
    filter.iff_ioctl = NULL;
    filter.iff_detached = &REACProtocol::filterDetachedFunc;
    
    if (0 != iflt_attach(interface, &filter, &filterRef)) {
        return false;
    }
    
    listening = true;
    
    return true;
}

void REACProtocol::detach() {
    if (listening) {
        if (NULL != timerEventSource) {
            timerEventSource->cancelTimeout();
            workLoop->removeEventSource(timerEventSource);
        }
        
        if (isConnected()) {
            // Announce disconnect
            if (NULL != connectionCallback) {
                connectionCallback(this, &cookieA, &cookieB, NULL);
            }
        }
        
        iflt_detach(filterRef);
        listening = false;
    }
}


errno_t REACProtocol::pushSamples(int numSamples, UInt8 *samples) {
    if (!(REAC_MASTER == mode || REAC_SLAVE == mode) || REAC_SAMPLES_PER_PACKET != numSamples)
        return EINVAL;
    
    return ENOSYS;
}

const REACDeviceInfo* REACProtocol::getDeviceInfo() const {
    return deviceInfo;
}

void REACProtocol::timerFired(OSObject *target, IOTimerEventSource *sender) {
    REACProtocol *proto = OSDynamicCast(REACProtocol, target);
    if (NULL == proto) {
        // This should never happen
        IOLog("REACProtocol::timerFired(): Internal error.\n");
        return;
    }
    
    if (!proto->connected) {
        return;
    }
    
    if ((proto->connectionCounter - proto->lastSeenConnectionCounter)*REAC_CONNECTION_CHECK_TIMEOUT > REAC_TIMEOUT_UNTIL_DISCONNECT) {
        proto->connected = false;
        if (NULL != proto->connectionCallback) {
            proto->connectionCallback(proto, &proto->cookieA, &proto->cookieB, NULL);
        }
    }
    
    proto->connectionCounter++;
    
    sender->setTimeoutMS(REAC_CONNECTION_CHECK_TIMEOUT);
}

void REACProtocol::filterCommandGateMsg(OSObject *target, void *data_mbuf, void*, void*, void*) {
    REACProtocol *proto = OSDynamicCast(REACProtocol, target);
    if (NULL == proto) {
        // This should never happen
        IOLog("REACProtocol::filterCommandGateMsg(): Internal error.\n");
        return;
    }
    
    const int samplesSize = REAC_SAMPLES_PER_PACKET*REAC_RESOLUTION*proto->deviceInfo->in_channels;
    
    mbuf_t *data;
    UInt32 len;
    REACPacketHeader packetHeader;
    
    data = (mbuf_t *)data_mbuf;
    
    len = 0;
    mbuf_t dataToCalcLength = *data;
    do {
        len += mbuf_len(dataToCalcLength);
    } while ((dataToCalcLength = mbuf_next(dataToCalcLength)));
    
    if (sizeof(REACPacketHeader)+samplesSize+sizeof(UInt16) != len) {
        IOLog("REACProtocol[%p]::filterCommandGateMsg(): Got packet of invalid length\n", proto);
        return;
    }
    
    if (0 != mbuf_copydata(*data, 0, sizeof(REACPacketHeader), &packetHeader)) {
        IOLog("REACProtocol[%p]::filterCommandGateMsg(): Failed to fetch REAC packet header\n", proto);
        return;
    }
    
    // TODO Check if the ending of the packet is right. ATM not as easy to implement as it
    // was when we copied the mbuf to a contiguous OSMalloc'd buffer.
    //
    // if (REAC_ENDING != (*((UInt16*)(((char*)buf)+sizeof(REACPacketHeader)+samplesSize)))) {
    //     // Incorrect ending. Not a REAC packet?
    //     goto Done;
    // }
    
    if (proto->connected /* This prunes a lost packet message when connecting */ && proto->lastCounter+1 != packetHeader.counter) {
        if (!(65535 == proto->lastCounter && 0 == packetHeader.counter)) {
            IOLog("REACProtocol[%p]::filterCommandGateMsg(): Lost packet [%d %d]\n",
                  proto, proto->lastCounter, packetHeader.counter);
        }
    }
    
    // Hack: Announce connect
    if (!proto->connected) {
        proto->connected = true;
        if (NULL != proto->connectionCallback) {
            proto->connectionCallback(proto, &proto->cookieA, &proto->cookieB, proto->deviceInfo);
        }
    }
    
    // Save the time we got the packet, for use by REACProtocol::timerFired
    //IOLog("AA: %llu   %llu\n", proto->lastSeenConnectionCounter, proto->connectionCounter); // TODO Debug
    proto->lastSeenConnectionCounter = proto->connectionCounter;
    
    if (proto->connected) {
        if (NULL != proto->samplesCallback) {
            proto->samplesCallback(proto, &proto->cookieA, &proto->cookieB,
                                   data, sizeof(REACPacketHeader), sizeof(REACPacketHeader)+samplesSize);
        }
    }
    proto->processDataStream(&packetHeader);
    
    proto->lastCounter = packetHeader.counter;
}


errno_t REACProtocol::filterInputFunc(void *cookie,
                                      ifnet_t interface, 
                                      protocol_family_t protocol,
                                      mbuf_t *data,
                                      char **frame_ptr) {
    REACProtocol *proto = (REACProtocol *)cookie;
    
    char *header = *frame_ptr;
    if (!(0x88 == ((UInt8*)header)[12] && 0x19 == ((UInt8*)header)[13])) {
        // This is not a REAC packet. Ignore.
        return 0; // Continue normal processing of the package.
    }
    
    proto->filterCommandGate->runCommand(data);
    
    return EINPROGRESS; // Skip the processing of the package.
}

void REACProtocol::filterDetachedFunc(void *cookie,
                                      ifnet_t interface) {
    // IOLog("REACProtocol[%p]::filterDetachedFunc()\n", cookie);
}

void REACProtocol::processDataStream(const REACPacketHeader* packet) {
    UInt16 fill;
    
    switch (packet->type) {
        case REAC_STREAM_FILLER:
            fill = packet->data[0];
            for (int i=1; i<16; i++) {
                if (packet->data[i] != fill) {
                    IOLog("REACProtocol[%p]::processDataStream(): Unexpected type 0 packet!\n", this);
                    break;
                }
            }
            
            break;
            
        case REAC_STREAM_CONTROL:
        case REAC_STREAM_CONTROL2:
            // IOLog("REACProtocol[%p]::processDataStream(): Got control [%d]\n", this, packet->counter);
            break;

    }
}

bool REACProtocol::checkChecksum(const REACPacketHeader* packet) const {
    u_char expected_checksum = 0;
    for (int i=0; i<31; i++) {
        if (i%2)
            expected_checksum += (u_char) (packet->data[i/2] >> 8);
        else
            expected_checksum += (u_char) packet->data[i/2] & 255;
    }
    expected_checksum = (u_char) (256 - (int)expected_checksum);
    
    return expected_checksum == packet->data[15] >> 8;
}
