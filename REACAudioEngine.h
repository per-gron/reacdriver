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
    REACProtocol       *protocol;
    
    UInt32              mInBufferSize;
    void               *mInBuffer;
    UInt32              mOutBufferSize;
    void               *mOutBuffer;
    
    IOAudioStream      *outputStream;
    IOAudioStream      *inputStream;

    UInt32              mLastValidSampleFrame;
    
    // FIXME for REAC_MASTER: IOTimerEventSource *timerEventSource;
    
    UInt32              blockSize;                // In sample frames -- fixed, as defined in the Info.plist (e.g. 8192)
    UInt32              numBlocks;
    UInt32              currentBlock;
    
    UInt64              blockTimeoutNS;
    UInt64              nextTime;                // the estimated time the timer will fire next

    bool                duringHardwareInit;
    
    
public:
    
	// class members
    static const SInt32 kVolumeMax;
    static const SInt32 kGainMax;

    virtual bool init(REACProtocol* proto, OSDictionary *properties);
    virtual void free();
    
    virtual bool initHardware(IOService *provider);
    
    virtual bool createAudioStreams(IOAudioSampleRate *initialSampleRate);

    virtual IOReturn performAudioEngineStart();
    virtual IOReturn performAudioEngineStop();
    
    virtual UInt32 getCurrentSampleFrame();
    
    virtual IOReturn performFormatChange(IOAudioStream *audioStream, const IOAudioStreamFormat *newFormat, const IOAudioSampleRate *newSampleRate);

    virtual IOReturn clipOutputSamples(const void *mixBuf, void *sampleBuf, UInt32 firstSampleFrame, UInt32 numSampleFrames, const IOAudioStreamFormat *streamFormat, IOAudioStream *audioStream);
    virtual IOReturn convertInputSamples(const void *sampleBuf, void *destBuf, UInt32 firstSampleFrame, UInt32 numSampleFrames, const IOAudioStreamFormat *streamFormat, IOAudioStream *audioStream);
    
    static void ourTimerFired(OSObject *target, IOTimerEventSource *sender);
    
    void gotSamples(int numSamples, UInt8 *samples);
    
};


#endif /* _REACAUDIOENGINE_H */
