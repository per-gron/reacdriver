/*
 *  MbufUtils.cpp
 *  REAC
 *
 *  Created by Per Eckerdal on 15/03/2011.
 *  Copyright 2011 Per Eckerdal. All rights reserved.
 *
 */

#include "MbufUtils.h"

#include <IOKit/IOLib.h>
#include "REACConnection.h"

size_t MbufUtils::mbufTotalLength(mbuf_t mbuf) {
    size_t len = 0;
    do {
        len += mbuf_len(mbuf);
    } while ((mbuf = mbuf_next(mbuf)));
    return len;
}

#define next_mbuf_macro() \
    mbuf = mbuf_next(mbuf); \
    if (!mbuf) { \
        /* This should never happen */ \
        IOLog("MbufUtils::next_mbuf_macro(): Internal error (couldn't fetch next mbuf).\n"); \
        return kIOReturnInternalError; \
    } \
    mbufBuffer = (UInt8 *)mbuf_data(mbuf); \
    mbufLength = mbuf_len(mbuf);

#define ensure_mbuf_macro() \
    while (0 == mbufLength) { \
        next_mbuf_macro(); \
    }

#define skip_mbuf_macro() \
    { \
        UInt32 skip = from; \
        while (skip) { \
            if (skip >= mbufLength) { \
                skip -= mbufLength; \
                next_mbuf_macro(); \
            } \
            else { \
                mbufLength -= skip; \
                mbufBuffer += skip; \
                skip = 0; \
            } \
        } \
    }

IOReturn MbufUtils::zeroMbuf(mbuf_t mbuf, UInt32 from, UInt32 len) {
    if (len > (UInt32) MbufUtils::mbufTotalLength(mbuf) - from) {
        IOLog("MbufUtils::zeroMbuf(): Got insufficiently large buffer (mbuf too small).\n");
        return kIOReturnNoMemory;
    }
    
    UInt8 *mbufBuffer = (UInt8 *)mbuf_data(mbuf);
    size_t mbufLength = mbuf_len(mbuf);
    UInt32 bytesLeft = len;
    
    skip_mbuf_macro();
    
    while (bytesLeft) {
        ensure_mbuf_macro();
        *mbufBuffer = 0;
        --bytesLeft;
    }
    
    return kIOReturnSuccess;
}

IOReturn MbufUtils::copyFromBufferToMbuf(mbuf_t mbuf, UInt32 from, UInt32 bufferSize, UInt8 *inBuffer) {
    if (bufferSize > (UInt32) MbufUtils::mbufTotalLength(mbuf)-from) {
        IOLog("MbufUtils::copyFromBufferToMbuf(): Got insufficiently large buffer (mbuf too small).\n");
        return kIOReturnNoMemory;
    }
    
    if (0 != bufferSize % (REAC_RESOLUTION*2)) {
        IOLog("MbufUtils::copyFromBufferToMbuf(): Buffer size must be a multiple of %d.\n", REAC_RESOLUTION*2);
        return kIOReturnBadArgument;
    }
    
    UInt8 *inBufferEnd = inBuffer + bufferSize;
    UInt8 *mbufBuffer = (UInt8 *)mbuf_data(mbuf);
    size_t mbufLength = mbuf_len(mbuf);
    
    skip_mbuf_macro();

#   define mbuf_move_buffer_forward_macro() \
        ++mbufBuffer; \
        --mbufLength; \
        ensure_mbuf_macro();
    
    while (inBuffer < inBufferEnd) {
        *mbufBuffer = inBuffer[1]; mbuf_move_buffer_forward_macro();
        *mbufBuffer = inBuffer[0]; mbuf_move_buffer_forward_macro();
        *mbufBuffer = inBuffer[3]; mbuf_move_buffer_forward_macro();
        
        *mbufBuffer = inBuffer[2]; mbuf_move_buffer_forward_macro();
        *mbufBuffer = inBuffer[5]; mbuf_move_buffer_forward_macro();
        *mbufBuffer = inBuffer[4]; mbuf_move_buffer_forward_macro();
        
        inBuffer += REAC_RESOLUTION*2;
    }
    
    return kIOReturnSuccess;
}

IOReturn MbufUtils::copyFromMbufToBuffer(mbuf_t mbuf, UInt32 from, UInt32 bufferSize, UInt8 *inBuffer) {
    if (bufferSize > (UInt32) MbufUtils::mbufTotalLength(mbuf)-from) {
        IOLog("MbufUtils::copyFromMbufToBuffer(): Got insufficiently large buffer (mbuf too small).\n");
        return kIOReturnNoMemory;
    }
    
    if (0 != bufferSize % (REAC_RESOLUTION*2)) {
        IOLog("MbufUtils::copyFromMbufToBuffer(): Buffer size must be a multiple of %d.\n", REAC_RESOLUTION*2);
        return kIOReturnBadArgument;
    }
    
    UInt8 *inBufferEnd = inBuffer + bufferSize;
    UInt8 intermediaryBuffer[6];
    UInt8 *mbufBuffer = (UInt8 *)mbuf_data(mbuf);
    size_t mbufLength = mbuf_len(mbuf);
    
    skip_mbuf_macro();
    
    while (inBuffer < inBufferEnd) {
        for (UInt32 i=0; i<sizeof(intermediaryBuffer); i++) {
            ensure_mbuf_macro();
            
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
