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

class MbufUtils {
public:
    static size_t mbufTotalLength(mbuf_t mbuf);
    static IOReturn zeroMbuf(mbuf_t mbuf, UInt32 from, UInt32 len);
    static IOReturn copyFromBufferToMbuf(mbuf_t mbuf, UInt32 from, UInt32 bufferSize, UInt8 *inBuffer);
    static IOReturn copyFromMbufToBuffer(mbuf_t mbuf, UInt32 from, UInt32 bufferSize, UInt8 *inBuffer);
};


#endif
