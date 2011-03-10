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
#define NUM_STREAMS_KEY                 "NumStreams"
#define FORMATS_KEY                     "Formats"
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
    
	// class members
	
    static const SInt32 kVolumeMax;
    static const SInt32 kGainMax;

	
	// instance members
    OSArray* protocols;

	SInt32 mVolume[17];
    SInt32 mMuteOut[17];
    SInt32 mMuteIn[17];
    SInt32 mGain[17];

	
	// methods
	
    virtual bool init(OSDictionary *properties); 
    virtual bool initHardware(IOService *provider);
    virtual void stop(IOService *provider);
    virtual void free();
    virtual bool createProtocolListeners();
    static void samplesCallback(REACProtocol *proto, void **cookieA, void** cookieB, int numSamples, UInt8 *samples);
    static void connectionCallback(REACProtocol *proto, void **cookieA, void** cookieB, REACDeviceInfo *device);
    virtual REACAudioEngine* createAudioEngine(REACProtocol* proto);
    virtual IOReturn performPowerStateChange(IOAudioDevicePowerState oldPowerState, 
                                             IOAudioDevicePowerState newPowerState,
                                             UInt32 *microsecondsUntilComplete);
    
    virtual bool initControls(REACAudioEngine *audioEngine);
    
    static  IOReturn volumeChangeHandler(IOService *target, IOAudioControl *volumeControl, SInt32 oldValue, SInt32 newValue);
    virtual IOReturn volumeChanged(IOAudioControl *volumeControl, SInt32 oldValue, SInt32 newValue);
    
    static  IOReturn outputMuteChangeHandler(IOService *target, IOAudioControl *muteControl, SInt32 oldValue, SInt32 newValue);
    virtual IOReturn outputMuteChanged(IOAudioControl *muteControl, SInt32 oldValue, SInt32 newValue);

    static  IOReturn gainChangeHandler(IOService *target, IOAudioControl *gainControl, SInt32 oldValue, SInt32 newValue);
    virtual IOReturn gainChanged(IOAudioControl *gainControl, SInt32 oldValue, SInt32 newValue);
    
    static  IOReturn inputMuteChangeHandler(IOService *target, IOAudioControl *muteControl, SInt32 oldValue, SInt32 newValue);
    virtual IOReturn inputMuteChanged(IOAudioControl *muteControl, SInt32 oldValue, SInt32 newValue);
    
};

#endif // _REACAUDIODEVICE_H
