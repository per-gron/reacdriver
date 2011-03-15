/*
 *  MbufUtils.h
 *  REAC
 *
 *  Created by Per Eckerdal on 15/03/2011.
 *  Copyright 2011 Per Eckerdal. All rights reserved.
 *
 */

#ifndef _MBUFUTILS_H
#define _MBUFUTILS_H

#include <libkern/OSTypes.h>
#include <libkern/c++/OSObject.h>
#include <IOKit/IOReturn.h>
#include <sys/kpi_mbuf.h>

#define MbufUtils          com_pereckerdal_driver_MbufUtils

// TODO Private constructor?
class MbufUtils {
    // Returns the new size of the mbuf
    inline static size_t attemptToSetLength(mbuf_t mbuf, size_t targetLength);
public:
    // On failure, this function may leave the mbuf in an inconsistent state (length wise, still safe to free)
    // This function can only increase the length
    static IOReturn setChainLength(mbuf_t mbuf, size_t targetLength);
    static size_t mbufTotalLength(mbuf_t mbuf);
    static size_t mbufTotalMaxLength(mbuf_t mbuf);
    static IOReturn zeroMbuf(mbuf_t mbuf, UInt32 from, UInt32 len);
    static IOReturn copyFromBufferToMbuf(mbuf_t mbuf, UInt32 from, UInt32 bufferSize, void *inBuffer);
    static IOReturn copyAudioFromBufferToMbuf(mbuf_t mbuf, UInt32 from, UInt32 bufferSize, UInt8 *inBuffer);
    static IOReturn copyAudioFromMbufToBuffer(mbuf_t mbuf, UInt32 from, UInt32 bufferSize, UInt8 *inBuffer);
};


#endif
