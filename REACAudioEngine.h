/*
  File:REACAudioEngine.h
*/

#ifndef _REACAUDIOENGINE_H
#define _REACAUDIOENGINE_H

#include <IOKit/audio/IOAudioEngine.h>

#include "REACDevice.h"

#define REACAudioEngine				com_pereckerdal_driver_REACAudioEngine

class REACAudioEngine : public IOAudioEngine
{
    OSDeclareDefaultStructors(REACAudioEngine)
    
	UInt32				mBufferSize;
	void*				mBuffer;				// input/output buffer
    float*				mThruBuffer;			// intermediate buffer to pass in-->out
	
	IOAudioStream*		outputStream;
	IOAudioStream*		inputStream;
    	
	UInt32				mLastValidSampleFrame;
    
    IOTimerEventSource*	timerEventSource;
    
    UInt32				blockSize;				// In sample frames -- fixed, as defined in the Info.plist (e.g. 8192)
    UInt32				numBlocks;
    UInt32				currentBlock;
    
    UInt64				blockTimeoutNS;
    UInt64				nextTime;				// the estimated time the timer will fire next

    bool				duringHardwareInit;
    
	
public:

    virtual bool init(OSDictionary *properties);
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
    
};


#endif /* _REACAUDIOENGINE_H */
