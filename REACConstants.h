/*
 *  REACConstants.h
 *  REAC
 *
 *  Created by Per Eckerdal on 15/03/2011.
 *  Copyright 2011 Per Eckerdal. All rights reserved.
 *  
 *  
 *  This file is part of the OS X REAC driver.
 *  
 *  The OS X REAC driver is free software: you can redistribute it
 *  and/or modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation, either version 3 of
 *  the License, or (at your option) any later version.
 *  
 *  The OS X REAC driver is distributed in the hope that it will be
 *  useful, but WITHOUT ANY WARRANTY; without even the implied warranty
 *  of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *  
 *  You should have received a copy of the GNU General Public License
 *  along with OS X REAC driver.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#ifndef _REACCONSTANTS_H
#define _REACCONSTANTS_H

#include <libkern/OSTypes.h>

#define REAC_MAX_CHANNEL_COUNT 40
#define REAC_PACKETS_PER_SECOND 8000
#define REAC_RESOLUTION 3 // 3 bytes per sample per channel
#define REAC_SAMPLES_PER_PACKET 12

#define REAC_SAMPLE_RATE REAC_PACKETS_PER_SECOND * REAC_SAMPLES_PER_PACKET

#define REACConstants          com_pereckerdal_driver_REACConstants


class REACConstants {
public:
    static const UInt8 ENDING[2];
    static const UInt8 PROTOCOL[2];
};


#endif
