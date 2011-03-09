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
#include <sys/errno.h>


REACProtocol* REACProtocol::listen(ifnet_t interface, REACMode mode,
                                   reac_samples_callback_t samplesCallback,
                                   void* samplesCookie,
                                   reac_connection_callback_t connectionCallback,
                                   void* connectionCookie) {
    OSMallocTag mt = OSMalloc_Tagalloc("REAC packet memory", OSMT_DEFAULT);
    REACProtocol *proto = (REACProtocol*) OSMalloc(sizeof(REACProtocol), mt);
    if (NULL == proto) {
        goto Fail;
    }
    
    // Hack: Pretend to be connected immediately
    proto->deviceInfo = (REACDeviceInfo*) OSMalloc(sizeof(REACDeviceInfo), mt);
    if (NULL == proto->deviceInfo) {
        goto Fail;
    }
    proto->deviceInfo->addr[0] = 0x00;
    proto->deviceInfo->addr[1] = 0x40;
    proto->deviceInfo->addr[2] = 0xab;
    proto->deviceInfo->addr[3] = 0xc4;
    proto->deviceInfo->addr[4] = 0x80;
    proto->deviceInfo->addr[5] = 0xf6;
    proto->deviceInfo->in_channels = 16;
    proto->deviceInfo->out_channels = 8;
    proto->connected = true;
    
    proto->reacMallocTag = mt;
    proto->lastCounter = 0;
    proto->samplesCallback = samplesCallback;
    proto->samplesCookie = samplesCookie;
    proto->connectionCallback = connectionCallback;
    proto->connectionCookie = connectionCookie;
    proto->mode = mode;
    
    iff_filter filter;
    
    filter.iff_cookie = proto;
    filter.iff_name = "REAC driver input filter";
    filter.iff_protocol = 0;
    filter.iff_input = &REACProtocol::filterInputFunc;
    filter.iff_output = NULL;
    filter.iff_event = NULL;
    filter.iff_ioctl = NULL;
    filter.iff_detached = &REACProtocol::filterDetachedFunc;
    
    if (0 != iflt_attach(interface, &filter, &proto->filterRef)) {
        goto Fail;
    }
    
    IOLog("REACProtocol[%p]::listen(): Listening\n", proto);
    
    // Hack: Announce connect
    if (NULL != proto->connectionCallback) {
        proto->connectionCallback(proto, connectionCookie, proto->deviceInfo);
    }
    
    return proto;
    
Fail:
    if (NULL != proto) {
        if (NULL != proto->deviceInfo) {
            OSFree(proto->deviceInfo, sizeof(REACDeviceInfo), mt);
        }
        OSFree(proto, sizeof(REACProtocol), mt);
    }
    OSMalloc_Tagfree(mt);
    return NULL;
}

void REACProtocol::detach() {
    if (isConnected()) {
        // Announce disconnect
        if (NULL != connectionCallback) {
            connectionCallback(this, connectionCookie, NULL);
        }
    }
    
    OSMallocTag mt = reacMallocTag;
    
    iflt_detach(filterRef);
    
    if (NULL != deviceInfo) {
        OSFree(deviceInfo, sizeof(REACDeviceInfo), mt);
    }
    
    OSFree(this, sizeof(REACProtocol), mt);
    OSMalloc_Tagfree(mt);
    
    IOLog("REACProtocol[%p]::listen(): Detached\n", this);
}


errno_t REACProtocol::pushSamples(UInt8 *samples[REAC_SAMPLES_PER_PACKET]) {
    if (!(REAC_MASTER == mode || REAC_SLAVE == mode))
        return EINVAL;
    
    return ENOSYS;
}

const REACDeviceInfo* REACProtocol::getDeviceInfo() const {
    return deviceInfo;
}

bool REACProtocol::isConnected() const {
    return connected;
}



errno_t REACProtocol::filterInputFunc(void *cookie,
                                      ifnet_t interface, 
                                      protocol_family_t protocol,
                                      mbuf_t *data,
                                      char **frame_ptr) {
    REACProtocol *proto = (REACProtocol*) cookie;
    int samplesSize = REAC_SAMPLES_PER_PACKET*REAC_RESOLUTION*proto->deviceInfo->in_channels;
    void *buf = NULL;
    REACPacketHeader *packetHeader = NULL;
    
    char *header = *frame_ptr;
    if (!(0x88 == ((UInt8*)header)[12] && 0x19 == ((UInt8*)header)[13])) {
        // This is not a REAC packet. Ignore.
        return 0; // Continue normal processing of the package.
    }
    
    UInt32 len = mbuf_len(*data);
    
    if (sizeof(REACPacketHeader)+samplesSize+sizeof(UInt16) != len) {
        IOLog("REACProtocol[%p]::filterInputFunc(): Got packet of invalid length\n", cookie);
    }
    
    buf = OSMalloc(len, proto->reacMallocTag);
    if (NULL == buf) {
        return EINPROGRESS; // Skip the processing of the package.
    }
    mbuf_copydata(*data, 0, len, buf);
    
    if (REAC_ENDING != (*((UInt16*)(((char*)buf)+sizeof(REACPacketHeader)+samplesSize)))) {
        // Incorrect ending. Not a REAC packet?
        goto Done;
    }
    
    packetHeader = (REACPacketHeader*) buf;
    
    if (proto->lastCounter+1 != packetHeader->counter) {
        if (!(65535 == proto->lastCounter && 0 == packetHeader->counter)) {
            IOLog("REACProtocol[%p]::filterInputFunc(): Lost packet [%d %d]\n",
                  cookie, proto->lastCounter, packetHeader->counter);
        }
    }
    
    if (proto->connected) {
        if (NULL != proto->samplesCallback) {
            proto->samplesCallback(proto, proto->samplesCookie, (UInt8**) buf+sizeof(REACPacketHeader));
        }
    }
    proto->processDataStream(packetHeader);
    
    proto->lastCounter = packetHeader->counter;
    
Done:
    if (NULL != buf) {
        OSFree(buf, len, proto->reacMallocTag);
    }
    
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






#if 0

void got_packet(u_char *args, const struct pcap_pkthdr *header, const u_char *packet) {
    for (int i=0; i<record_channels; i++) {  
        for (int j=0; j<REAC_SAMPLES_PER_PACKET; j++) {
            if (0 == i%2) {
                buf[i][buffer_place+j*REAC_RESOLUTION  ] = reac->samples[j][i/2][3];
                buf[i][buffer_place+j*REAC_RESOLUTION+1] = reac->samples[j][i/2][0];
                buf[i][buffer_place+j*REAC_RESOLUTION+2] = reac->samples[j][i/2][1];
            }
            else {
                buf[i][buffer_place+j*REAC_RESOLUTION  ] = reac->samples[j][i/2][4];
                buf[i][buffer_place+j*REAC_RESOLUTION+1] = reac->samples[j][i/2][5];
                buf[i][buffer_place+j*REAC_RESOLUTION+2] = reac->samples[j][i/2][2];
            }
        }
    }
}


#endif // #if 0
