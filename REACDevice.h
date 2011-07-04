/*
 *  REACDevice.h
 *  REAC
 *
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

#ifndef _REACAUDIODEVICE_H
#define _REACAUDIODEVICE_H

#include <IOKit/audio/IOAudioDevice.h>

#include "REACConnection.h"

#define AUDIO_ENGINE_PARAMS_KEY         "AudioEngineParams"
#define INTERFACES_KEY                  "Interfaces"
#define INTERFACE_NAME_KEY              "Name"
#define DESCRIPTION_KEY                 "Description"
#define BLOCK_SIZE_KEY                  "BlockSize"
#define NUM_BLOCKS_KEY                  "NumBlocks"
#define BUFFER_OFFSET_FACTOR_KEY        "BufferOffsetFactor"
#define IN_FORMAT_KEY                   "InFormat"
#define OUT_FORMAT_KEY                  "OutFormat"
#define SAMPLE_RATES_KEY				"SampleRates"
#define SEPARATE_STREAM_BUFFERS_KEY     "SeparateStreamBuffers"
#define SEPARATE_INPUT_BUFFERS_KEY      "SeparateInputBuffers"

#define REACDevice				com_pereckerdal_driver_REACDevice
#define REACAudioEngine			com_pereckerdal_driver_REACAudioEngine

class REACAudioEngine;

class REACDevice : public IOAudioDevice
{
    OSDeclareDefaultStructors(REACDevice)
    friend class REACAudioEngine;
	
	// instance members
    OSArray *protocols;

	
	// methods
	
    virtual bool init(OSDictionary *properties); 
    virtual bool initHardware(IOService *provider);
    virtual void stop(IOService *provider);
    virtual void free();
    virtual bool createProtocolListeners();
    static void connectionCallback(REACConnection *proto, void **cookieA, void** cookieB, REACDeviceInfo *device);
    static void samplesCallback(REACConnection *proto, void **cookieA, void** cookieB, UInt8 **data, UInt32 *bufferSize);
    static void getSamplesCallback(REACConnection *proto, void **cookieA, void** cookieB, UInt8 **data, UInt32 *bufferSize);
    virtual REACAudioEngine* createAudioEngine(REACConnection *proto);
    virtual IOReturn performPowerStateChange(IOAudioDevicePowerState oldPowerState, 
                                             IOAudioDevicePowerState newPowerState,
                                             UInt32 *microsecondsUntilComplete);
    
};

#endif // _REACAUDIODEVICE_H
