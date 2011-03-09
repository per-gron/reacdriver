/*
  File:REACDevice.h
*/

#ifndef _REACAUDIODEVICE_H
#define _REACAUDIODEVICE_H

#include <IOKit/audio/IOAudioDevice.h>

#include "REACProtocol.h"

#define AUDIO_ENGINES_KEY				"AudioEngines"
#define DESCRIPTION_KEY					"Description"
#define BLOCK_SIZE_KEY					"BlockSize"
#define NUM_BLOCKS_KEY					"NumBlocks"
#define NUM_STREAMS_KEY					"NumStreams"
#define FORMATS_KEY						"Formats"
#define SAMPLE_RATES_KEY				"SampleRates"
#define SEPARATE_STREAM_BUFFERS_KEY		"SeparateStreamBuffers"
#define SEPARATE_INPUT_BUFFERS_KEY		"SeparateInputBuffers"

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
    
    REACProtocol* proto;

	SInt32 mVolume[17];
    SInt32 mMuteOut[17];
    SInt32 mMuteIn[17];
    SInt32 mGain[17];

	
	// methods
	
    virtual bool initHardware(IOService *provider);
    virtual void stop(IOService *provider);
    virtual bool createAudioEngines();
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

#endif // _SAMPLEAUDIODEVICE_H
