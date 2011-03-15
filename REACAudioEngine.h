/*
  File:REACAudioEngine.h
*/

#ifndef _REACAUDIOENGINE_H
#define _REACAUDIOENGINE_H

#include <IOKit/audio/IOAudioEngine.h>

#include "REACDevice.h"

#define REACAudioEngine                com_pereckerdal_driver_REACAudioEngine

class REACAudioEngine : public IOAudioEngine
{
    OSDeclareDefaultStructors(REACAudioEngine)
    
    // instance members
    REACConnection     *protocol;
    
    UInt32              mInBufferSize;
    void               *mInBuffer;
    UInt32              mOutBufferSize;
    void               *mOutBuffer;
    
    IOAudioStream      *outputStream;
    IOAudioStream      *inputStream;

    UInt32              mLastValidSampleFrame;
    
	SInt32              mVolume[17];
    SInt32              mMuteOut[17];
    SInt32              mMuteIn[17];
    SInt32              mGain[17];
    
    UInt32              blockSize;                // In sample frames -- fixed, as defined in the Info.plist (e.g. 8192)
    UInt32              numBlocks;
    UInt32              bufferOffsetFactor;
    UInt32              currentBlock;

    bool                duringHardwareInit;
    
    // For clipping routines
    UInt64              lastSampleTimeNS;
    
    
public:
    
	// class members
    static const SInt32 kVolumeMax;
    static const SInt32 kGainMax;

    virtual bool init(REACConnection* proto, OSDictionary *properties);
    virtual void free();
    
    virtual bool initHardware(IOService *provider);
    
    virtual bool createAudioStreams(IOAudioSampleRate *initialSampleRate);

    virtual IOReturn performAudioEngineStart();
    virtual IOReturn performAudioEngineStop();
    
    virtual UInt32 getCurrentSampleFrame();
    
    virtual IOReturn performFormatChange(IOAudioStream *audioStream, const IOAudioStreamFormat *newFormat,
                                         const IOAudioSampleRate *newSampleRate);

    virtual IOReturn clipOutputSamples(const void *mixBuf, void *sampleBuf, UInt32 firstSampleFrame,
                                       UInt32 numSampleFrames, const IOAudioStreamFormat *streamFormat,
                                       IOAudioStream *audioStream);
    virtual IOReturn convertInputSamples(const void *sampleBuf, void *destBuf, UInt32 firstSampleFrame,
                                         UInt32 numSampleFrames, const IOAudioStreamFormat *streamFormat,
                                         IOAudioStream *audioStream);
    
    void gotSamples(UInt8 **data, UInt32 *bufferSize);
    
protected:
    virtual bool initControls();
    
    static  IOReturn volumeChangeHandler(IOService *target, IOAudioControl *volumeControl, SInt32 oldValue, SInt32 newValue);
    virtual IOReturn volumeChanged(IOAudioControl *volumeControl, SInt32 oldValue, SInt32 newValue);
    
    static  IOReturn outputMuteChangeHandler(IOService *target, IOAudioControl *muteControl, SInt32 oldValue, SInt32 newValue);
    virtual IOReturn outputMuteChanged(IOAudioControl *muteControl, SInt32 oldValue, SInt32 newValue);
    
    static  IOReturn gainChangeHandler(IOService *target, IOAudioControl *gainControl, SInt32 oldValue, SInt32 newValue);
    virtual IOReturn gainChanged(IOAudioControl *gainControl, SInt32 oldValue, SInt32 newValue);
    
    static  IOReturn inputMuteChangeHandler(IOService *target, IOAudioControl *muteControl, SInt32 oldValue, SInt32 newValue);
    virtual IOReturn inputMuteChanged(IOAudioControl *muteControl, SInt32 oldValue, SInt32 newValue);
    
};


#endif /* _REACAUDIOENGINE_H */
