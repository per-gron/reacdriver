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

#define REAC_CONNECTION_CHECK_TIMEOUT_MS 400
#define REAC_TIMEOUT_UNTIL_DISCONNECT 1000

#define super OSObject

OSDefineMetaClassAndStructors(REACConnection, OSObject)


bool REACConnection::initWithInterface(IOWorkLoop *workLoop_, ifnet_t interface_, REACMode mode_,
                                       reac_connection_callback_t connectionCallback_,
                                       reac_samples_callback_t samplesCallback_,
                                       reac_get_samples_callback_t getSamplesCallback_,
                                       void *cookieA_,
                                       void *cookieB_) {
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
    
    dataStream = REACDataStream::with();
    if (NULL == dataStream) {
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
    connectionCallback = connectionCallback_;
    samplesCallback = samplesCallback_;
    getSamplesCallback = getSamplesCallback_;
    cookieA = cookieA_;
    cookieB = cookieB_;
    mode = mode_;
    
    ifnet_reference(interface_);
    interface = interface_;
    
    // Calculate our timeout in nanosecs, taking care to keep 64bits
    if (REAC_MASTER == mode_) {
        timeoutNS = 1000000000;
        timeoutNS /= REAC_PACKETS_PER_SECOND;
    }
    else {
        timeoutNS = REAC_CONNECTION_CHECK_TIMEOUT_MS;
        timeoutNS *= 1000000;
    }
    
    return true;
    
Fail:
    deinit();
    return false;
}

REACConnection *REACConnection::withInterface(IOWorkLoop *workLoop, ifnet_t interface, REACMode mode,
                                              reac_connection_callback_t connectionCallback,
                                              reac_samples_callback_t samplesCallback,
                                              reac_get_samples_callback_t getSamplesCallback,
                                              void *cookieA,
                                              void *cookieB) {
    REACConnection *p = new REACConnection;
    if (NULL == p) return NULL;
    bool result = p->initWithInterface(workLoop, interface, mode, connectionCallback,
                                       samplesCallback, getSamplesCallback, cookieA, cookieB);
    if (!result) {
        p->release();
        return NULL;
    }
    return p;
}

void REACConnection::deinit() {
    detach();
    
    if (NULL != dataStream) {
        dataStream->release();
        dataStream = NULL;
    }
    
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
    
    
    timerEventSource->setTimeout(timeoutNS);
    
    uint64_t time;
    clock_get_uptime(&time);
    absolutetime_to_nanoseconds(time, &nextTime);
    nextTime += timeoutNS;
        
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

const REACDeviceInfo *REACConnection::getDeviceInfo() const {
    return deviceInfo;
}

void REACConnection::timerFired(OSObject *target, IOTimerEventSource *sender) {
    REACConnection *proto = OSDynamicCast(REACConnection, target);
    if (NULL == proto) {
        // This should never happen
        IOLog("REACConnection::timerFired(): Internal error!\n");
        return;
    }
    
    if (proto->isConnected()) {
        if ((proto->connectionCounter - proto->lastSeenConnectionCounter)*proto->timeoutNS >
                (UInt64)REAC_TIMEOUT_UNTIL_DISCONNECT*1000000) {
            proto->connected = true;
            if (NULL != proto->connectionCallback) {
                proto->connectionCallback(proto, &proto->cookieA, &proto->cookieB, NULL);
            }
        }
        
        proto->connectionCounter++;
    }
    
    if (REAC_MASTER == proto->mode) {
        proto->getAndPushSamples();
    }
    
    // Calculate next time to fire, by taking the time and comparing it to the time we requested.                                 
    UInt64            thisTimeNS;
    uint64_t          time;
    SInt64            diff;
    clock_get_uptime(&time);
    absolutetime_to_nanoseconds(time, &thisTimeNS);
    // This next calculation must be signed or we will introduce distortion after only a couple of vectors
    diff = ((SInt64)proto->nextTime - (SInt64)thisTimeNS);
    sender->setTimeout(proto->timeoutNS + diff);
    proto->nextTime += proto->timeoutNS;
}

size_t REACConnection::mbufTotalLength(mbuf_t mbuf) {
    size_t len = 0;
    do {
        len += mbuf_len(mbuf);
    } while ((mbuf = mbuf_next(mbuf)));
    return len;
}

#define next_mbuf() \
    mbuf = mbuf_next(mbuf); \
    if (!mbuf) { \
        /* This should never happen */ \
        IOLog("REACConnection::next_mbuf(): Internal error (couldn't fetch next mbuf).\n"); \
        return kIOReturnInternalError; \
    } \
    mbufBuffer = (UInt8 *)mbuf_data(mbuf); \
    mbufLength = mbuf_len(mbuf);

#define ensure_mbuf() \
    while (0 == mbufLength) { \
        next_mbuf(); \
    }

#define skip_mbuf() \
    { \
        UInt32 skip = from; \
        while (skip) { \
            if (skip >= mbufLength) { \
                skip -= mbufLength; \
                next_mbuf(); \
            } \
            else { \
                mbufLength -= skip; \
                mbufBuffer += skip; \
                skip = 0; \
            } \
        } \
    }

IOReturn REACConnection::zeroMbuf(mbuf_t mbuf, UInt32 from, UInt32 len) {
    if (len > (UInt32) REACConnection::mbufTotalLength(mbuf) - from) {
        IOLog("REACConnection::copyFromMbufToBuffer(): Got insufficiently large buffer (mbuf too small).\n");
        return kIOReturnNoMemory;
    }
    
    UInt8 *mbufBuffer = (UInt8 *)mbuf_data(mbuf);
    size_t mbufLength = mbuf_len(mbuf);
    UInt32 bytesLeft = len;
    
    skip_mbuf();
    
    while (bytesLeft) {
        ensure_mbuf();
        *mbufBuffer = 0;
        --bytesLeft;
    }
    
    return kIOReturnSuccess;
}

IOReturn REACConnection::copyFromBufferToMbuf(REACDeviceInfo *di, mbuf_t mbuf, UInt32 from, UInt32 bufferSize, UInt8 *inBuffer) {
    // TODO Implement me
    return kIOReturnError;
}

IOReturn REACConnection::copyFromMbufToBuffer(REACDeviceInfo *di, mbuf_t mbuf, UInt32 from, UInt32 bufferSize, UInt8 *inBuffer) {
    const UInt32 bytesPerSample = REAC_RESOLUTION * di->in_channels;
    const UInt32 bytesPerPacket = bytesPerSample * REAC_SAMPLES_PER_PACKET;
    
    UInt8 *inBufferEnd = inBuffer + bufferSize;
    
    if (bufferSize != bytesPerPacket) {
        IOLog("REACConnection::copyFromMbufToBuffer(): Got incorrectly sized buffer (not the same as a packet).\n");
        return kIOReturnNoMemory;
    }
    
    if (bufferSize > (UInt32) REACConnection::mbufTotalLength(mbuf)-from) {
        IOLog("REACConnection::copyFromMbufToBuffer(): Got insufficiently large buffer (mbuf too small).\n");
        return kIOReturnNoMemory;
    }
    
    UInt8 intermediaryBuffer[6];
    UInt8 *mbufBuffer = (UInt8 *)mbuf_data(mbuf);
    size_t mbufLength = mbuf_len(mbuf);
    
    skip_mbuf();
    
    while (inBuffer < inBufferEnd) {
        for (UInt32 i=0; i<sizeof(intermediaryBuffer); i++) {
            ensure_mbuf();
            
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
    
    return kIOReturnSuccess;
}

IOReturn REACConnection::getAndPushSamples() {
    UInt8 *sampleBuffer = NULL;
    UInt32 bufSize = 0;
    if (getSamplesCallback) {
        getSamplesCallback(this, &cookieA, &cookieB, &sampleBuffer, &bufSize);
    }
    return pushSamples(bufSize, sampleBuffer);
}

IOReturn REACConnection::pushSamples(UInt32 bufSize, UInt8 *sampleBuffer) {
    const UInt32 ending = REAC_ENDING; // TODO This is just so hacky wrt size and endianness
    const UInt32 samplesSize = REAC_SAMPLES_PER_PACKET*REAC_RESOLUTION*deviceInfo->out_channels;
    const UInt32 packetLen = sizeof(REACPacketHeader)+samplesSize+REAC_ENDING_LENGTH;
    REACPacketHeader rph;
    mbuf_t mbuf = NULL;
    int result = kIOReturnError;
    
    if (!(REAC_SLAVE == mode || REAC_MASTER == mode)) {
        result = kIOReturnInvalid;
        goto Done;
    }
    
    if (samplesSize == bufSize && NULL != sampleBuffer) { // bufSize is ignored when sampleBuffer is NULL
        result = kIOReturnBadArgument;
        goto Done;
    }
    
    dataStream->processPacket(&rph);
    
    if (0 != mbuf_allocpacket(MBUF_DONTWAIT, packetLen, NULL, &mbuf)) {
        IOLog("REACConnection::pushSamples() - Error: Failed to allocate packet mbuf.");
        goto Done;
    }
    if (0 != mbuf_copyback(mbuf, 0, sizeof(REACPacketHeader), &rph, MBUF_DONTWAIT)) {
        IOLog("REACConnection::pushSamples() - Error: Failed to copy REAC header to packet mbuf.");
        goto Done;
    }
    if (NULL == sampleBuffer) {
        if (kIOReturnSuccess != REACConnection::zeroMbuf(mbuf, sizeof(REACPacketHeader), samplesSize)) {
            IOLog("REACConnection::pushSamples() - Error: Failed to zero sample data in mbuf.");
            goto Done;
        }
    }
    else {
        if (kIOReturnSuccess != REACConnection::copyFromBufferToMbuf(deviceInfo, mbuf, sizeof(REACPacketHeader),
                                                                     bufSize, sampleBuffer)) {
            IOLog("REACConnection::pushSamples() - Error: Failed to copy sample data to packet mbuf.");
            goto Done;
        }
    }
    // TODO Invent a better way to handle REAC_ENDING
    if (0 != mbuf_copyback(mbuf, sizeof(REACPacketHeader)+samplesSize, REAC_ENDING_LENGTH, &ending, MBUF_DONTWAIT)) {
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

void REACConnection::filterCommandGateMsg(OSObject *target, void *data_mbuf, void*, void*, void*) {
    REACConnection *proto = OSDynamicCast(REACConnection, target);
    if (NULL == proto) {
        // This should never happen
        IOLog("REACConnection::filterCommandGateMsg(): Internal error.\n");
        return;
    }
    
    const int samplesSize = REAC_SAMPLES_PER_PACKET*REAC_RESOLUTION*proto->deviceInfo->in_channels;
    
    mbuf_t *data = (mbuf_t *)data_mbuf;
    UInt32 len = REACConnection::mbufTotalLength(*data);
    REACPacketHeader packetHeader;
    
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
    
    if (proto->isConnected() /* This prunes a lost packet message when connecting */ && proto->lastCounter+1 != packetHeader.counter) {
        if (!(65535 == proto->lastCounter && 0 == packetHeader.counter)) {
            IOLog("REACConnection[%p]::filterCommandGateMsg(): Lost packet [%d %d]\n",
                  proto, proto->lastCounter, packetHeader.counter);
        }
    }
    
    // Hack: Announce connect
    if (!proto->isConnected()) {
        proto->connected = true;
        if (NULL != proto->connectionCallback) {
            proto->connectionCallback(proto, &proto->cookieA, &proto->cookieB, proto->deviceInfo);
        }
    }
    
    // Save the time we got the packet, for use by REACConnection::timerFired
    proto->lastSeenConnectionCounter = proto->connectionCounter;
    
    
    if (proto->isConnected()) {
        if (NULL != proto->samplesCallback) {
            UInt8* inBuffer = NULL;
            UInt32 inBufferSize = 0;
            proto->samplesCallback(proto, &proto->cookieA, &proto->cookieB, &inBuffer, &inBufferSize);
            
            if (NULL != inBuffer) {
                REACConnection::copyFromMbufToBuffer(proto->deviceInfo, *data, sizeof(REACPacketHeader),
                                                     inBufferSize, inBuffer);
            }
            
            proto->dataStream->gotPacket(&packetHeader);
        }
        
        if (REAC_SLAVE == proto->mode) {
            proto->getAndPushSamples();
        }
    }
    
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
