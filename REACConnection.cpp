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

#include "MbufUtils.h"

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
    started = false;
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
    stop();
    
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


bool REACConnection::start() {
    if (NULL == timerEventSource || workLoop->addEventSource(timerEventSource) != kIOReturnSuccess) {
        IOLog("REACConnection::start() - Error: Failed to add timer event source to work loop!\n");
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
    
    started = true;
    
    return true;
}

void REACConnection::stop() {
    if (started) {
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
        started = false;
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

IOReturn REACConnection::getAndPushSamples() {
    UInt8 *sampleBuffer = NULL;
    UInt32 bufSize = 0;
    if (getSamplesCallback) {
        getSamplesCallback(this, &cookieA, &cookieB, &sampleBuffer, &bufSize);
    }
    return pushSamples(bufSize, sampleBuffer);
}

IOReturn REACConnection::pushSamples(UInt32 bufSize, UInt8 *sampleBuffer) {
    // TODO Are we supposed to write the ethernet header here as well? I think so?
    
    const UInt32 samplesSize = REAC_SAMPLES_PER_PACKET*REAC_RESOLUTION*deviceInfo->out_channels;
    const UInt32 packetLen = sizeof(REACPacketHeader)+samplesSize+sizeof(REACConstants::ENDING);
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
    
    if (kIOReturnSuccess != dataStream->processPacket(&rph)) {
        IOLog("REACConnection::pushSamples() - Error: Failed to process packet data stream.");
        goto Done;
    }
    
    if (0 != mbuf_allocpacket(MBUF_DONTWAIT, packetLen, NULL, &mbuf)) {
        IOLog("REACConnection::pushSamples() - Error: Failed to allocate packet mbuf.");
        goto Done;
    }
    
    // ifnet_get_address_list  &  ifnet_free_address_list
    // ifaddr_address
    EthernetHeader header;
    memset(&header.shost, 0xff, ETHER_ADDR_LEN); // TODO
    memset(&header.dhost, 0xff, ETHER_ADDR_LEN);
    memcpy(&header.type, REACConstants::PROTOCOL, sizeof(REACConstants::PROTOCOL));
    mbuf_pkthdr_setheader(mbuf, &header);
    mbuf_pkthdr_setlen(mbuf, sizeof(EthernetHeader));
    
    if (0 != mbuf_copyback(mbuf, 0, sizeof(REACPacketHeader), &rph, MBUF_DONTWAIT)) {
        IOLog("REACConnection::pushSamples() - Error: Failed to copy REAC header to packet mbuf.");
        goto Done;
    }
    if (NULL == sampleBuffer) {
        if (kIOReturnSuccess != MbufUtils::zeroMbuf(mbuf, sizeof(REACPacketHeader), samplesSize)) {
            IOLog("REACConnection::pushSamples() - Error: Failed to zero sample data in mbuf.");
            goto Done;
        }
    }
    else {
        if (kIOReturnSuccess != MbufUtils::copyFromBufferToMbuf(mbuf, sizeof(REACPacketHeader),
                                                                bufSize, sampleBuffer)) {
            IOLog("REACConnection::pushSamples() - Error: Failed to copy sample data to packet mbuf.");
            goto Done;
        }
    }
    if (0 != mbuf_copyback(mbuf, sizeof(REACPacketHeader)+samplesSize, sizeof(REACConstants::ENDING),
                           REACConstants::ENDING, MBUF_DONTWAIT)) {
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
    UInt32 len = MbufUtils::mbufTotalLength(*data);
    REACPacketHeader packetHeader;
    
    // Check packet length
    if (sizeof(REACPacketHeader)+samplesSize+sizeof(UInt16) != len) {
        IOLog("REACConnection[%p]::filterCommandGateMsg(): Got packet of invalid length\n", proto);
        return;
    }
    
    // Check packet ending
    UInt8 packetEnding[sizeof(REACConstants::ENDING)];
    if (0 != mbuf_copydata(*data, sizeof(REACPacketHeader)+samplesSize, sizeof(REACConstants::ENDING), &packetEnding)) {
        IOLog("REACConnection[%p]::filterCommandGateMsg(): Failed to fetch REAC packet ending\n", proto);
        return;
    }
    if (0 != memcmp(packetEnding, REACConstants::ENDING, sizeof(packetEnding))) {
        // Incorrect ending. Not a REAC packet?
        IOLog("REACConnection[%p]::filterCommandGateMsg(): Incorrect packet ending.\n", proto);
        return;
    }
    
    // Fetch packet header
    if (0 != mbuf_copydata(*data, 0, sizeof(REACPacketHeader), &packetHeader)) {
        IOLog("REACConnection[%p]::filterCommandGateMsg(): Failed to fetch REAC packet header\n", proto);
        return;
    }
    
    // Check packet counter
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
                const UInt32 bytesPerSample = REAC_RESOLUTION * proto->deviceInfo->in_channels;
                const UInt32 bytesPerPacket = bytesPerSample * REAC_SAMPLES_PER_PACKET;
                
                if (inBufferSize != bytesPerPacket) {
                    IOLog("REACConnection::copyFromMbufToBuffer(): Got incorrectly sized buffer (not the same as a packet).\n");
                }
                else {
                    MbufUtils::copyFromMbufToBuffer(*data, sizeof(REACPacketHeader), inBufferSize, inBuffer);
                }
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
    if (!(REACConstants::PROTOCOL[0] == ((UInt8*)header)[12] && REACConstants::PROTOCOL[1] == ((UInt8*)header)[13])) {
        // This is not a REAC packet. Ignore.
        return 0; // Continue normal processing of the package.
    }
    
    // TODO The input filter function must make sure to not parse the outgoing packets
    
    proto->filterCommandGate->runCommand(data);
    
    return EINPROGRESS; // Skip the processing of the package.
}

void REACConnection::filterDetachedFunc(void *cookie,
                                        ifnet_t interface) {
    // IOLog("REACConnection[%p]::filterDetachedFunc()\n", cookie);
}
