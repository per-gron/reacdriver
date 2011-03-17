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
#include <sys/socket.h>

#include "MbufUtils.h"

#define REAC_CONNECTION_CHECK_TIMEOUT_MS 500
#define REAC_TIMEOUT_UNTIL_DISCONNECT 1000

#define super OSObject

OSDefineMetaClassAndStructors(REACConnection, OSObject)

bool REACConnection::initWithInterface(IOWorkLoop *workLoop_, ifnet_t interface_, REACMode mode_,
                                       reac_connection_callback_t connectionCallback_,
                                       reac_samples_callback_t samplesCallback_,
                                       reac_get_samples_callback_t getSamplesCallback_,
                                       void *cookieA_,
                                       void *cookieB_,
                                       UInt8 inChannels_,
                                       UInt8 outChannels_) {
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
    
    dataStream = REACDataStream::withConnection(this);
    if (NULL == dataStream) {
        goto Fail;
    }
    
    // Hack: Pretend to be connected immediately
    deviceInfo = (REACDeviceInfo*) IOMalloc(sizeof(REACDeviceInfo));
    if (NULL == deviceInfo) {
        IOLog("REACConnection::initWithInterface() - Error: Failed to allocate device info object.\n");
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
    lastSeenConnectionCounter = 0;
    lastSentAnnouncementCounter = 0;
    splitAnnouncementCounter = 0;
    connectionCallback = connectionCallback_;
    samplesCallback = samplesCallback_;
    getSamplesCallback = getSamplesCallback_;
    cookieA = cookieA_;
    cookieB = cookieB_;
    mode = mode_;
    inChannels = inChannels_;
    outChannels = outChannels_;
    
    ifnet_reference(interface_);
    interface = interface_;
    
    if (kIOReturnSuccess != REACConnection::getInterfaceMacAddress(interface, interfaceAddr, sizeof(interfaceAddr))) {
        IOLog("REACConnection::initWithInterface() - Error: Failed to get interface address.\n");
        goto Fail;
    }
    
    // TODO This is a hack. It seems to be needless though.
    //static const UInt8 counterfeitMac[] = {
    //    0x00, 0x40, 0xab, 0xc4, 0xb7, 0x58
    //};
    //memcpy(interfaceAddr, counterfeitMac, sizeof(interfaceAddr));
    
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
                                              void *cookieB,
                                              UInt8 inChannels,
                                              UInt8 outChannels) {
    REACConnection *p = new REACConnection;
    if (NULL == p) return NULL;
    bool result = p->initWithInterface(workLoop, interface, mode, connectionCallback, samplesCallback,
                                       getSamplesCallback, cookieA, cookieB, inChannels, outChannels);
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
    
    UInt64            thisTimeNS;
    uint64_t          time;
    SInt64            diff;
    
    do {
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
        else if (REAC_SPLIT == proto->mode) {
            proto->lastSentAnnouncementCounter++;
            if (proto->lastSentAnnouncementCounter*proto->timeoutNS >= 1000000000) {
                proto->lastSentAnnouncementCounter = 0;
                proto->pushSplitAnnouncementPacket();
            }
        }
        
        // Calculate next time to fire, by taking the time and comparing it to the time we requested.
        clock_get_uptime(&time);
        absolutetime_to_nanoseconds(time, &thisTimeNS);
        proto->nextTime += proto->timeoutNS;
        // This next calculation must be signed
        diff = ((SInt64)proto->nextTime - (SInt64)thisTimeNS);
        
        if (diff < -((SInt64)proto->timeoutNS)*10) {
            // TODO After a certain amount of lost packets we probably ought to skip
            IOLog("REACConnection::timerFired(): Lost the time by %lld us\n", diff/1000);
        }
    } while (diff < 0);
    sender->setTimeout(diff);
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
    const UInt32 samplesSize = REAC_SAMPLES_PER_PACKET*REAC_RESOLUTION*
                                (REAC_MASTER == mode ? inChannels : deviceInfo->out_channels);
    const UInt32 sampleOffset = sizeof(EthernetHeader)+sizeof(REACPacketHeader);
    const UInt32 endingOffset = sampleOffset+samplesSize;
    const UInt32 packetLen = endingOffset+sizeof(REACConstants::ENDING);
    REACPacketHeader rph;
    mbuf_t mbuf = NULL;
    int result = kIOReturnError;
    
    /// Do some argument checks
    if (!(REAC_SLAVE == mode || REAC_MASTER == mode)) {
        result = kIOReturnInvalid;
        goto Done;
    }
    if (samplesSize == bufSize && NULL != sampleBuffer) { // bufSize is ignored when sampleBuffer is NULL
        result = kIOReturnBadArgument;
        goto Done;
    }
    
    /// Do REAC data stream processing
    if (kIOReturnSuccess != dataStream->processPacket(&rph)) {
        IOLog("REACConnection::pushSamples() - Error: Failed to process packet data stream.\n");
        goto Done;
    }
    
    /// Allocate mbuf
    if (0 != mbuf_allocpacket(MBUF_DONTWAIT, packetLen, NULL, &mbuf) ||
        kIOReturnSuccess != MbufUtils::setChainLength(mbuf, packetLen)) {
        IOLog("REACConnection::pushSamples() - Error: Failed to allocate packet mbuf.\n");
        goto Done;
    }
    
    /// Copy ethernet header
    EthernetHeader header;
    memcpy(header.shost, interfaceAddr, sizeof(header.shost));
    if (REAC_SLAVE == mode) {
        memcpy(header.dhost, deviceInfo->addr, sizeof(header.dhost));
    }
    else {
        memset(header.dhost, 0xff, ETHER_ADDR_LEN);
    }
    memcpy(&header.type, REACConstants::PROTOCOL, sizeof(REACConstants::PROTOCOL));
    if (kIOReturnSuccess != MbufUtils::copyFromBufferToMbuf(mbuf, 0, sizeof(EthernetHeader), &header)) {
        IOLog("REACConnection::pushSamples() - Error: Failed to copy REAC header to packet mbuf.\n");
        goto Done;
    }
    
    /// Copy REAC header
    if (kIOReturnSuccess != MbufUtils::copyFromBufferToMbuf(mbuf, sizeof(EthernetHeader), sizeof(REACPacketHeader), &rph)) {
        IOLog("REACConnection::pushSamples() - Error: Failed to copy REAC header to packet mbuf.\n");
        goto Done;
    }
    
    /// Copy sample data
    if (NULL != sampleBuffer) {
        if (kIOReturnSuccess != MbufUtils::copyAudioFromBufferToMbuf(mbuf, sampleOffset, bufSize, sampleBuffer)) {
            IOLog("REACConnection::pushSamples() - Error: Failed to copy sample data to packet mbuf.\n");
            goto Done;
        }
    }
    else {
        if (kIOReturnSuccess != MbufUtils::zeroMbuf(mbuf, sampleOffset, samplesSize)) {
            IOLog("REACConnection::pushSamples() - Error: Failed to zero sample data in mbuf.\n");
            goto Done;
        }
    }
    
    /// Copy packet ending
    if (kIOReturnSuccess != MbufUtils::copyFromBufferToMbuf(mbuf, endingOffset,
                                                            sizeof(REACConstants::ENDING), (void *)REACConstants::ENDING)) {
        IOLog("REACConnection::pushSamples() - Error: Failed to copy ending to packet mbuf.\n");
        goto Done;
    }
    
    /// Send packet
    if (0 != ifnet_output_raw(interface, 0, mbuf)) {
        mbuf = NULL; // ifnet_output_raw always frees the mbuf
        IOLog("REACConnection::pushSamples() - Error: Failed to send packet.\n");
        goto Done;
    }
    
    mbuf = NULL; // ifnet_output_raw always frees the mbuf
    result = kIOReturnSuccess;
Done:
    if (NULL != mbuf) {
        mbuf_freem(mbuf);
        mbuf = NULL;
    }
    return result;
}

IOReturn REACConnection::pushSplitAnnouncementPacket() {
    const UInt32 fillerSize = 288;
    const UInt32 fillerOffset = sizeof(EthernetHeader)+sizeof(REACPacketHeader);
    const UInt32 endingOffset = fillerOffset+fillerSize;
    const UInt32 packetLen = endingOffset+sizeof(REACConstants::ENDING);
    REACPacketHeader rph;
    mbuf_t mbuf = NULL;
    int result = kIOReturnError;
    
    /// Do some argument checks
    if (!REAC_SPLIT == mode) {
        result = kIOReturnInvalid;
        goto Done;
    }
    
    /// Allocate mbuf
    if (0 != mbuf_allocpacket(MBUF_DONTWAIT, packetLen, NULL, &mbuf) ||
        kIOReturnSuccess != MbufUtils::setChainLength(mbuf, packetLen)) {
        IOLog("REACConnection::pushSplitAnnouncementPacket() - Error: Failed to allocate packet mbuf.\n");
        goto Done;
    }
    
    /// Copy ethernet header
    EthernetHeader header;
    memcpy(header.shost, interfaceAddr, sizeof(header.shost));
    memcpy(header.dhost, deviceInfo->addr, sizeof(header.dhost));
    memcpy(&header.type, REACConstants::PROTOCOL, sizeof(REACConstants::PROTOCOL));
    if (kIOReturnSuccess != MbufUtils::copyFromBufferToMbuf(mbuf, 0, sizeof(EthernetHeader), &header)) {
        IOLog("REACConnection::pushSplitAnnouncementPacket() - Error: Failed to copy REAC header to packet mbuf.\n");
        goto Done;
    }
    
    /// Prepare REAC packet header
    rph.setCounter(splitAnnouncementCounter++);
    dataStream->prepareSplitAnnounce(&rph);    
    
    /// Copy REAC header
    if (kIOReturnSuccess != MbufUtils::copyFromBufferToMbuf(mbuf, sizeof(EthernetHeader), sizeof(REACPacketHeader), &rph)) {
        IOLog("REACConnection::pushSplitAnnouncementPacket() - Error: Failed to copy REAC header to packet mbuf.\n");
        goto Done;
    }
    
    /// Copy filler
    if (kIOReturnSuccess != MbufUtils::zeroMbuf(mbuf, fillerOffset, fillerSize)) {
        IOLog("REACConnection::pushSplitAnnouncementPacket() - Error: Failed to zero filler data in mbuf.\n");
        goto Done;
    }
    
    /// Copy packet ending
    if (kIOReturnSuccess != MbufUtils::copyFromBufferToMbuf(mbuf, endingOffset,
                                                            sizeof(REACConstants::ENDING), (void *)REACConstants::ENDING)) {
        IOLog("REACConnection::pushSplitAnnouncementPacket() - Error: Failed to copy ending to packet mbuf.\n");
        goto Done;
    }
    
    /// Send packet
    if (0 != ifnet_output_raw(interface, 0, mbuf)) {
        mbuf = NULL; // ifnet_output_raw always frees the mbuf
        IOLog("REACConnection::pushSplitAnnouncementPacket() - Error: Failed to send packet.\n");
        goto Done;
    }
    
    mbuf = NULL; // ifnet_output_raw always frees the mbuf
    result = kIOReturnSuccess;
Done:
    if (NULL != mbuf) {
        mbuf_freem(mbuf);
        mbuf = NULL;
    }
    return result;
}

void REACConnection::filterCommandGateMsg(OSObject *target, void *data_mbuf, void *eth_header_ptr, void*, void*) {
    REACConnection *proto = OSDynamicCast(REACConnection, target);
    if (NULL == proto) {
        // This should never happen
        IOLog("REACConnection::filterCommandGateMsg(): Internal error.\n");
        return;
    }
    
    const EthernetHeader *ethernetHeader = (const EthernetHeader *)eth_header_ptr;
    const int samplesSize = REAC_SAMPLES_PER_PACKET*REAC_RESOLUTION*proto->deviceInfo->in_channels;
    
    mbuf_t *data = (mbuf_t *)data_mbuf;
    UInt32 len = MbufUtils::mbufTotalLength(*data);
    REACPacketHeader packetHeader;
    
    // Check that the packet length is long enough
    if (len < sizeof(REACPacketHeader)+sizeof(REACConstants::ENDING)) {
        IOLog("REACConnection[%p]::filterCommandGateMsg(): Got packet of too short length\n", proto);
        return;
    }
        
    // Check packet ending
    UInt8 packetEnding[sizeof(REACConstants::ENDING)];
    if (0 != mbuf_copydata(*data, len-sizeof(REACConstants::ENDING), sizeof(REACConstants::ENDING), &packetEnding)) {
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
    if (proto->isConnected() && /* This prunes a lost packet message when connecting */
        proto->lastCounter+1 != packetHeader.getCounter()) {
        if (!(65535 == proto->lastCounter && 0 == packetHeader.counter)) {
            IOLog("REACConnection[%p]::filterCommandGateMsg(): Lost packet [%d %d]\n",
                  proto, proto->lastCounter, packetHeader.getCounter());
        }
    }
    
    // Process packet header
    proto->dataStream->gotPacket(&packetHeader, ethernetHeader);
    
    // Check packet length
    if (sizeof(REACPacketHeader)+samplesSize+sizeof(UInt16) == len) {
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
                        IOLog("REACConnection::filterCommandGateMsg(): Got incorrectly sized buffer (not the same as a packet).\n");
                    }
                    else {
                        MbufUtils::copyAudioFromMbufToBuffer(*data, sizeof(REACPacketHeader), inBufferSize, inBuffer);
                    }
                }
            }
            
            if (REAC_SLAVE == proto->mode) {
                proto->getAndPushSamples();
            }
        }
    }
    
    proto->lastCounter = packetHeader.getCounter();
}


errno_t REACConnection::filterInputFunc(void *cookie,
                                        ifnet_t interface, 
                                        protocol_family_t protocol,
                                        mbuf_t *data,
                                        char **frame_ptr) {
    REACConnection *proto = (REACConnection *)cookie;
    
    EthernetHeader *header = (EthernetHeader *)*frame_ptr;
    if (0 != memcmp(header->type, REACConstants::PROTOCOL, sizeof(header->type))) {
        // This is not a REAC packet. Ignore.
        return 0; // Continue normal processing of the package.
    }
        
    proto->filterCommandGate->runCommand(data, header);
    
    return EINPROGRESS; // Skip the processing of the package.
}

void REACConnection::filterDetachedFunc(void *cookie,
                                        ifnet_t interface) {
    // IOLog("REACConnection[%p]::filterDetachedFunc()\n", cookie);
}


// I can't find this class in any header (??)
#define REACSockaddr              com_pereckerdal_driver_REACSockaddr
struct REACSockaddr {
    u_char  sdl_len;        /* Total length of sockaddr */
    u_char  sdl_family;     /* AF_LINK */
    u_short sdl_index;      /* if != 0, system given index for interface */
    u_char  sdl_type;       /* interface type */
    u_char  sdl_nlen;       /* interface name length, no trailing 0 reqd. */
    u_char  sdl_alen;       /* link level address length */
    u_char  sdl_slen;       /* link layer selector length */
    char    sdl_data[16];   /* minimum work area, can be larger;
                             contains both if name and ll address */
};

IOReturn REACConnection::getInterfaceMacAddress(ifnet_t interface, UInt8* resultAddr, UInt32 addrLen) {
    ifaddr_t *addresses;
    ifaddr_t *address;
    REACSockaddr addr;
    IOReturn ret = kIOReturnError;
    
    if (ETHER_ADDR_LEN != addrLen) {
        return kIOReturnBadArgument;
    }
    
    if (ifnet_get_address_list_family(interface, &addresses, AF_LINK)) {
        return kIOReturnError;
    }
    
    address = addresses;
    while (*address) {
        if (0 == ifaddr_address(*address, (sockaddr*) &addr, sizeof(addr))) {
            if (addr.sdl_alen == ETHER_ADDR_LEN) {
                memcpy(resultAddr, addr.sdl_data+addr.sdl_nlen, addrLen);
                ret = kIOReturnSuccess;
                break;
            }
        }
        ++address;
    }
    
    ifnet_free_address_list(addresses);
    return ret;
}
