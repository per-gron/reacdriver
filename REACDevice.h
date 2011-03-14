/*
  File:REACDevice.h
*/

#ifndef _REACAUDIODEVICE_H
#define _REACAUDIODEVICE_H

#include <IOKit/audio/IOAudioDevice.h>

#include "REACProtocol.h"

#define AUDIO_ENGINE_PARAMS_KEY         "AudioEngineParams"
#define INTERFACES_KEY                  "Interfaces"
#define INTERFACE_NAME_KEY              "Name"
#define DESCRIPTION_KEY                 "Description"
#define REAC_PROTOCOL_KEY               "REACProtocol"
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
    static void samplesCallback(REACProtocol *proto, void **cookieA, void** cookieB, mbuf_t *data, int from, int to);
    static void connectionCallback(REACProtocol *proto, void **cookieA, void** cookieB, REACDeviceInfo *device);
    virtual REACAudioEngine* createAudioEngine(REACProtocol* proto);
    virtual IOReturn performPowerStateChange(IOAudioDevicePowerState oldPowerState, 
                                             IOAudioDevicePowerState newPowerState,
                                             UInt32 *microsecondsUntilComplete);
    
};

#endif // _REACAUDIODEVICE_H
