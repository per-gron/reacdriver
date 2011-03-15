/*
 *  REACConstants.h
 *  REAC
 *
 *  Created by Per Eckerdal on 15/03/2011.
 *  Copyright 2011 Per Eckerdal. All rights reserved.
 *
 */

#ifndef _REACCONSTANTS_H
#define _REACCONSTANTS_H

#include <libkern/OSTypes.h>

#define REAC_MAX_CHANNEL_COUNT 40
#define REAC_PACKETS_PER_SECOND 8000
#define REAC_RESOLUTION 3 // 3 bytes per sample per channel
#define REAC_SAMPLES_PER_PACKET 12
#define ETHER_ADDR_LEN 6

#define REAC_SAMPLE_RATE REAC_PACKETS_PER_SECOND * REAC_SAMPLES_PER_PACKET

#define REACConstants          com_pereckerdal_driver_REACConstants


class REACConstants {
public:
    static const UInt8 ENDING[2];
    static const UInt8 PROTOCOL[2];
};


#endif
