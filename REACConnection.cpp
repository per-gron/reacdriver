/*
 *  REACConnection.cpp
 *  REAC
 *
 *  Created by Per Eckerdal on 02/01/2011.
 *  Copyright 2011 Per Eckerdal. All rights reserved.
 *
 */

#include "REACConnection.h"

#include <IOKit/IOLib.h>
#include <IOKit/IOTimerEventSource.h>
#include <sys/errno.h>

#define REAC_CONNECTION_CHECK_TIMEOUT 400
#define REAC_TIMEOUT_UNTIL_DISCONNECT 1000

#define super OSObject

OSDefineMetaClassAndStructors(REACConnection, OSObject)


bool REACConnection::initWithInterface(IOWorkLoop *workLoop_, ifnet_t interface_, REACMode mode_,
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
        IOLog("REACConnection::initWithInterface() - Error: Can't create or add commandGate\n");
        goto Fail;
    }
    
    // Add the timer event source to the workloop
    connectionCounter = 0;
    lastSeenConnectionCounter = 0;
    timerEventSource = IOTimerEventSource::timerEventSource(this, (IOTimerEventSource::Action)&REACConnection::timerFired);
    if (NULL == timerEventSource) {
        IOLog("REACConnection::initWithInterface() - Error: Failed to create timer event source.\n");
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

REACConnection* REACConnection::withInterface(IOWorkLoop *workLoop, ifnet_t interface, REACMode mode,
                                              reac_connection_callback_t connectionCallback,
                                              reac_samples_callback_t samplesCallback,
                                              void* cookieA,
                                              void* cookieB) {
    REACConnection* p = new REACConnection;
    if (NULL == p) return NULL;
    bool result = p->initWithInterface(workLoop, interface, mode, connectionCallback, samplesCallback, cookieA, cookieB);
    if (!result) {
        p->release();
        return NULL;
    }
    return p;
}

void REACConnection::deinit() {
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

void REACConnection::free() {
    deinit();
    super::free();
}


bool REACConnection::listen() {
    if (NULL == timerEventSource || workLoop->addEventSource(timerEventSource) != kIOReturnSuccess) {
        IOLog("REACConnection::listen() - Error: Failed to add timer event source to work loop!\n");
        return false;
    }
    timerEventSource->setTimeoutMS(REAC_CONNECTION_CHECK_TIMEOUT);
    
    iff_filter filter;
    filter.iff_cookie = this;
    filter.iff_name = "REAC driver input filter";
    filter.iff_protocol = 0;
    filter.iff_input = &REACConnection::filterInputFunc;
    filter.iff_output = NULL;
    filter.iff_event = NULL;
    filter.iff_ioctl = NULL;
    filter.iff_detached = &REACConnection::filterDetachedFunc;
    
    if (0 != iflt_attach(interface, &filter, &filterRef)) {
        return false;
    }
    
    listening = true;
    
    return true;
}

void REACConnection::detach() {
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


IOReturn REACConnection::pushSamples(UInt32 bufSize, UInt8 *sampleBuffer) {
    int result = kIOReturnError;
    
    if (REAC_MASTER != mode ||
        REAC_SAMPLES_PER_PACKET*REAC_RESOLUTION*deviceInfo->out_channels == bufSize)
        return EINVAL;
    
    const int packetLen = sizeof(REACPacketHeader)+bufSize+REAC_ENDING_LENGTH;
    
    REACPacketHeader rph;
    UInt32 ending = REAC_ENDING; // TODO This is just so hacky wrt size and endianness
    
    mbuf_t mbuf = NULL;
    if (0 != mbuf_allocpacket(MBUF_DONTWAIT, packetLen, NULL, &mbuf)) {
        IOLog("REACConnection::pushSamples() - Error: Failed to allocate packet mbuf.");
        goto Done;
    }
    if (0 != mbuf_copyback(mbuf, 0, sizeof(REACPacketHeader), &rph, MBUF_DONTWAIT)) {
        IOLog("REACConnection::pushSamples() - Error: Failed to copy REAC header to packet mbuf.");
        goto Done;
    }
    if (0 != mbuf_copyback(mbuf, sizeof(REACPacketHeader), bufSize, sampleBuffer, MBUF_DONTWAIT)) {
        IOLog("REACConnection::pushSamples() - Error: Failed to copy sample data to packet mbuf.");
        goto Done;
    }
    if (0 != mbuf_copyback(mbuf, sizeof(REACPacketHeader)+bufSize, REAC_ENDING_LENGTH, &ending, MBUF_DONTWAIT)) {
        IOLog("REACConnection::pushSamples() - Error: Failed to copy ending to packet mbuf.");
        goto Done;
    }
    
    if (0 != ifnet_output_raw(interface, 0, mbuf)) {
        mbuf = NULL; // ifnet_output_raw always frees the mbuf
        IOLog("REACConnection::pushSamples() - Error: Failed to send packet.");
        goto Done;
    }
    
    mbuf = NULL; // ifnet_output_raw always frees the mbuf
    result = kIOReturnSuccess;
Done:
    if (NULL != mbuf) {
        mbuf_free(mbuf);
        mbuf = NULL;
    }
    return result;
}

const REACDeviceInfo* REACConnection::getDeviceInfo() const {
    return deviceInfo;
}

void REACConnection::timerFired(OSObject *target, IOTimerEventSource *sender) {
    REACConnection *proto = OSDynamicCast(REACConnection, target);
    if (NULL == proto) {
        // This should never happen
        IOLog("REACConnection::timerFired(): Internal error.\n");
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

void REACConnection::copyFromMbufToBuffer(REACDeviceInfo *di, mbuf_t *data, int from, int to, UInt8 *inBuffer, int bufferSize) {
    const int bytesPerSample = REAC_RESOLUTION * di->in_channels;
    const int bytesPerPacket = bytesPerSample * REAC_SAMPLES_PER_PACKET;
    
    UInt8 *inBufferEnd = inBuffer + bufferSize;
    
    if (bufferSize < bytesPerPacket) {
        IOLog("REACConnection::copyFromMbufToBuffer(): Got insufficiently large buffer.\n");
        return;
    }
    
    if (to-from != bytesPerPacket) {
        IOLog("REACConnection::copyFromMbufToBuffer(): Got packet of invalid length.\n");
        return;
    }
    
    UInt8 intermediaryBuffer[6];
    mbuf_t mbuf = *data;
    UInt8 *mbufBuffer = (UInt8 *)mbuf_data(mbuf);
    size_t mbufLength = mbuf_len(mbuf);
    
#   define next_mbuf() \
        mbuf = mbuf_next(mbuf); \
        if (!mbuf) { \
            /* This should never happen */ \
            IOLog("REACConnection::copyFromMbufToBuffer(): Internal error (couldn't fetch next mbuf).\n"); \
            return; \
        } \
        mbufBuffer = (UInt8 *)mbuf_data(mbuf); \
        mbufLength = mbuf_len(mbuf);
    
    UInt32 skip = from;
    while (skip) {
        if (skip >= mbufLength) {
            skip -= mbufLength;
            next_mbuf();
        }
        else {
            mbufLength -= skip;
            mbufBuffer += skip;
            skip = 0;
        }
    }
    
    while (inBuffer < inBufferEnd) {
        for (UInt32 i=0; i<sizeof(intermediaryBuffer); i++) {
            while (0 == mbufLength) {
                next_mbuf();
            }
            
            intermediaryBuffer[i] = *mbufBuffer;
            ++mbufBuffer;
            --mbufLength;
        }
        
        inBuffer[0] = intermediaryBuffer[1];
        inBuffer[1] = intermediaryBuffer[0];
        inBuffer[2] = intermediaryBuffer[3];
        
        inBuffer[3] = intermediaryBuffer[2];
        inBuffer[4] = intermediaryBuffer[5];
        inBuffer[5] = intermediaryBuffer[4];
        
        inBuffer += REAC_RESOLUTION*2;
    }
}

void REACConnection::filterCommandGateMsg(OSObject *target, void *data_mbuf, void*, void*, void*) {
    REACConnection *proto = OSDynamicCast(REACConnection, target);
    if (NULL == proto) {
        // This should never happen
        IOLog("REACConnection::filterCommandGateMsg(): Internal error.\n");
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
        IOLog("REACConnection[%p]::filterCommandGateMsg(): Got packet of invalid length\n", proto);
        return;
    }
    
    if (0 != mbuf_copydata(*data, 0, sizeof(REACPacketHeader), &packetHeader)) {
        IOLog("REACConnection[%p]::filterCommandGateMsg(): Failed to fetch REAC packet header\n", proto);
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
            IOLog("REACConnection[%p]::filterCommandGateMsg(): Lost packet [%d %d]\n",
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
    
    // Save the time we got the packet, for use by REACConnection::timerFired
    //IOLog("AA: %llu   %llu\n", proto->lastSeenConnectionCounter, proto->connectionCounter); // TODO Debug
    proto->lastSeenConnectionCounter = proto->connectionCounter;
    
    if (proto->connected && NULL != proto->samplesCallback) {
        UInt8* inBuffer = NULL;
        UInt32 inBufferSize = 0;
        proto->samplesCallback(proto, &proto->cookieA, &proto->cookieB, &inBuffer, &inBufferSize);
        
        if (NULL != inBuffer) {
            REACConnection::copyFromMbufToBuffer(proto->deviceInfo, data, sizeof(REACPacketHeader),
                                               sizeof(REACPacketHeader)+samplesSize, inBuffer, inBufferSize);
        }
    }
    
    // TODO: proto->processDataStream(&packetHeader);
    
    proto->lastCounter = packetHeader.counter;
}


errno_t REACConnection::filterInputFunc(void *cookie,
                                        ifnet_t interface, 
                                        protocol_family_t protocol,
                                        mbuf_t *data,
                                        char **frame_ptr) {
    REACConnection *proto = (REACConnection *)cookie;
    
    char *header = *frame_ptr;
    if (!(0x88 == ((UInt8*)header)[12] && 0x19 == ((UInt8*)header)[13])) {
        // This is not a REAC packet. Ignore.
        return 0; // Continue normal processing of the package.
    }
    
    proto->filterCommandGate->runCommand(data);
    
    return EINPROGRESS; // Skip the processing of the package.
}

void REACConnection::filterDetachedFunc(void *cookie,
                                      ifnet_t interface) {
    // IOLog("REACConnection[%p]::filterDetachedFunc()\n", cookie);
}
