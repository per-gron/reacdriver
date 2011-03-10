/*
 * File:REACAudioEngine.cpp
 */

#include "REACAudioEngine.h"

#include <IOKit/audio/IOAudioControl.h>
#include <IOKit/audio/IOAudioLevelControl.h>
#include <IOKit/audio/IOAudioToggleControl.h>
#include <IOKit/audio/IOAudioDefines.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>

#include "REACProtocol.h"

#define BLOCK_SIZE        REAC_SAMPLES_PER_PACKET        // Sample frames
#define NUM_BLOCKS        1024
#define NUM_STREAMS       1

#define super IOAudioEngine

OSDefineMetaClassAndStructors(REACAudioEngine, IOAudioEngine)


bool REACAudioEngine::init(REACProtocol* proto, OSDictionary *properties)
{
    bool result = false;
    OSNumber *number = NULL;
    
    //IOLog("REACAudioEngine[%p]::init()\n", this);
    
    if (NULL == proto) {
        goto Done;
    }
    protocol = proto;

    if (!super::init(properties)) {
        goto Done;
    }
    
    // Do class-specific initialization here
    // If no non-hardware initialization is needed, this function can be removed
    
    number = OSDynamicCast(OSNumber, getProperty(NUM_BLOCKS_KEY));
    numBlocks = (number ? number->unsigned32BitValue() : NUM_BLOCKS);
    
    number = OSDynamicCast(OSNumber, getProperty(BLOCK_SIZE_KEY));
    blockSize = (number ? number->unsigned32BitValue() : BLOCK_SIZE);
    
    inputStream = outputStream = NULL;
    duringHardwareInit = FALSE;
    mLastValidSampleFrame = 0;
    result = true;
    
Done:
    return result;
}


bool REACAudioEngine::initHardware(IOService *provider)
{
    bool result = false;
    IOAudioSampleRate initialSampleRate;
    IOWorkLoop *wl;
    
    //IOLog("REACAudioEngine[%p]::initHardware(%p)\n", this, provider);
    
    duringHardwareInit = TRUE;
    
    if (!super::initHardware(provider)) {
        goto Done;
    }
    
    initialSampleRate.whole = 0;
    initialSampleRate.fraction = 0;

    if (!createAudioStreams(&initialSampleRate)) {
        IOLog("REACAudioEngine::initHardware() failed\n");
        goto Done;
    }
    
    if (initialSampleRate.whole == 0) {
        goto Done;
    }
    
    // calculate our timeout in nanosecs, taking care to keep 64bits
    blockTimeoutNS = blockSize;
    blockTimeoutNS *= 1000000000;
    blockTimeoutNS /= initialSampleRate.whole;

    setSampleRate(&initialSampleRate);
    setSampleOffset(blockSize);
    
    // Set the number of sample frames in each buffer
    setNumSampleFramesPerBuffer(blockSize * numBlocks);
    
    wl = getWorkLoop();
    if (!wl) {
        goto Done;
    }
    
    timerEventSource = IOTimerEventSource::timerEventSource(this, ourTimerFired);
    
    if (!timerEventSource) {
        goto Done;
    }
    
    workLoop->addEventSource(timerEventSource);
        
    result = true;
    
Done:
    duringHardwareInit = FALSE;    
    return result;
}

 
bool REACAudioEngine::createAudioStreams(IOAudioSampleRate *initialSampleRate)
{
    bool            result = false;
    OSNumber       *number = NULL;
    UInt32          numStreams;
    UInt32          streamNum;
    OSArray        *formatArray = NULL;
    OSArray        *sampleRateArray = NULL;
    UInt32          startingChannelID = 1;
    OSString       *desc;
    
    desc = OSDynamicCast(OSString, getProperty(DESCRIPTION_KEY));
    if (desc)
        setDescription(desc->getCStringNoCopy());
    
    number = OSDynamicCast(OSNumber, getProperty(NUM_STREAMS_KEY));
    if (number)
        numStreams = number->unsigned32BitValue();
    else
        numStreams = NUM_STREAMS;
    
    formatArray = OSDynamicCast(OSArray, getProperty(FORMATS_KEY));
    if (formatArray == NULL) {
        IOLog("SF formatArray is NULL\n");
        goto Done;
    }
    
    sampleRateArray = OSDynamicCast(OSArray, getProperty(SAMPLE_RATES_KEY));
    if (sampleRateArray == NULL) {
        IOLog("SF sampleRateArray is NULL\n");
        goto Done;
    }
    
    for (streamNum = 0; streamNum < numStreams; streamNum++) {
        UInt32                  maxBitWidth = 0;
        UInt32                  maxNumChannels = 0;
        OSCollectionIterator   *formatIterator = NULL;
        OSCollectionIterator   *sampleRateIterator = NULL;
        OSDictionary           *formatDict;
        IOAudioSampleRate       sampleRate;
        IOAudioStreamFormat     initialFormat;
        bool                    initialFormatSet;
        UInt32                  channelID;
        char                    outputStreamName[64];
        char                    inputStreamName[64];
        
        initialFormatSet = false;
        
        sampleRate.whole = 0;
        sampleRate.fraction = 0;
        
        inputStream = new IOAudioStream;
        if (inputStream == NULL) {
            IOLog("SF could not create new input IOAudioStream\n");
            goto Error;
        }
        
        outputStream = new IOAudioStream;
        if (outputStream == NULL) {
            IOLog("SF could not create new output IOAudioStream\n");
            goto Error;
        }

        snprintf(inputStreamName, 64, "REAC Input Stream #%u", (unsigned int) (streamNum + 1));
        snprintf(outputStreamName, 64, "REAC Output Stream #%u", (unsigned int) streamNum + 1);

        if (!inputStream->initWithAudioEngine(this, kIOAudioStreamDirectionInput, startingChannelID, inputStreamName) ||
            !outputStream->initWithAudioEngine(this, kIOAudioStreamDirectionOutput, startingChannelID, outputStreamName)) {
            IOLog("SF could not init one of the streams with audio engine. \n");
            goto Error;
        }
        
        formatIterator = OSCollectionIterator::withCollection(formatArray);
        if (!formatIterator) {
            IOLog("SF NULL formatIterator\n");
            goto Error;
        }
        
        sampleRateIterator = OSCollectionIterator::withCollection(sampleRateArray);
        if (!sampleRateIterator) {
            IOLog("SF NULL sampleRateIterator\n");
            goto Error;
        }
        
        formatIterator->reset();
        while ((formatDict = (OSDictionary *)formatIterator->getNextObject())) {
            IOAudioStreamFormat format;
            
            if (OSDynamicCast(OSDictionary, formatDict) == NULL) {
                IOLog("SF error casting formatDict\n");
                goto Error;
            }
            
            if (IOAudioStream::createFormatFromDictionary(formatDict, &format) == NULL) {
                IOLog("SF error in createFormatFromDictionary()\n");
                goto Error;
            }
            
            if (!initialFormatSet) {
                initialFormat = format;
            }
            
            sampleRateIterator->reset();
            while ((number = (OSNumber *)sampleRateIterator->getNextObject())) {
                if (!OSDynamicCast(OSNumber, number)) {
                    IOLog("SF error iterating sample rates\n");
                    goto Error;
                }
                
                sampleRate.whole = number->unsigned32BitValue();
                
                inputStream->addAvailableFormat(&format, &sampleRate, &sampleRate);
                outputStream->addAvailableFormat(&format, &sampleRate, &sampleRate);
                
                if (format.fNumChannels > maxNumChannels) {
                    maxNumChannels = format.fNumChannels;
                }
                
                if (format.fBitWidth > maxBitWidth) {
                    maxBitWidth = format.fBitWidth;
               }
                
                if (initialSampleRate->whole == 0) {
                    initialSampleRate->whole = sampleRate.whole;
                }
            }
        }
        
        mBufferSize = blockSize * numBlocks * maxNumChannels * maxBitWidth / 8;
        //IOLog("REAC streamBufferSize: %ld\n", mBufferSize);
        
        if (mBuffer == NULL) {
            mBuffer = (void *)IOMalloc(mBufferSize);
            if (!mBuffer) {
                IOLog("REAC: Error allocating output buffer - %lu bytes.\n", (unsigned long)mBufferSize);
                goto Error;
            }
            
            mThruBuffer = (float*)IOMalloc(mBufferSize);
            if (!mThruBuffer) {
                IOLog("REAC: Error allocating thru buffer - %lu bytes.\n", (unsigned long)mBufferSize);
                goto Error;
            }
            memset((UInt8*)mThruBuffer, 0, mBufferSize);
        }
        
        inputStream->setFormat(&initialFormat);
        inputStream->setSampleBuffer(mBuffer, mBufferSize);
        addAudioStream(inputStream);
        inputStream->release();

        outputStream->setFormat(&initialFormat);
        outputStream->setSampleBuffer(mBuffer, mBufferSize);       
        addAudioStream(outputStream);
        outputStream->release();
        
        formatIterator->release();
        sampleRateIterator->release();
        
        for (channelID = startingChannelID; channelID < (startingChannelID + maxNumChannels); channelID++) {
            char channelName[20];
            
            snprintf(channelName, 20, "Channel %u", (unsigned int) channelID);
        }
        
        startingChannelID += maxNumChannels;
        
        continue;

Error:
        IOLog("REACAudioEngine[%p]::createAudioStreams() - ERROR\n", this);
    
        if (inputStream)
            inputStream->release();
        if (outputStream)
            outputStream->release();
        if (formatIterator)
            formatIterator->release();
        if (sampleRateIterator)
            sampleRateIterator->release();
        goto Done;
    }
    result = true;
    
Done:
    if (!result)
        IOLog("REACAudioEngine[%p]::createAudioStreams() - failed!\n", this);
    return result;
}

 
void REACAudioEngine::free()
{
    //IOLog("REACAudioEngine[%p]::free()\n", this);
    
    if (mBuffer) {
        IOFree(mBuffer, mBufferSize);
        mBuffer = NULL;
    }
    if (mThruBuffer) {
        IOFree(mThruBuffer, mBufferSize);
        mThruBuffer = NULL;
    }
    super::free();
}

IOReturn REACAudioEngine::performAudioEngineStart()
{
    // IOLog("REACAudioEngine[%p]::performAudioEngineStart()\n", this);
    
    // When performAudioEngineStart() gets called, the audio engine should be started from the beginning
    // of the sample buffer.  Because it is starting on the first sample, a new timestamp is needed
    // to indicate when that sample is being read from/written to.  The function takeTimeStamp() 
    // is provided to do that automatically with the current time.
    // By default takeTimeStamp() will increment the current loop count in addition to taking the current
    // timestamp.  Since we are starting a new audio engine run, and not looping, we don't want the loop count
    // to be incremented.  To accomplish that, false is passed to takeTimeStamp(). 
    
    // The audio engine will also have to take a timestamp each time the buffer wraps around
    // How that is implemented depends on the type of hardware - PCI hardware will likely
    // receive an interrupt to perform that task
    
    takeTimeStamp(false);
    currentBlock = 0;
    
    timerEventSource->setTimeout(blockTimeoutNS);
    
    uint64_t time;
    clock_get_uptime(&time);
    absolutetime_to_nanoseconds(time, &nextTime);

    nextTime += blockTimeoutNS;
    
    return kIOReturnSuccess;
}

IOReturn REACAudioEngine::performAudioEngineStop()
{
    //IOLog("REACAudioEngine[%p]::performAudioEngineStop()\n", this);
         
    timerEventSource->cancelTimeout();
    
    return kIOReturnSuccess;
}

UInt32 REACAudioEngine::getCurrentSampleFrame()
{
    //IOLog("REACAudioEngine[%p]::getCurrentSampleFrame() - currentBlock = %lu\n", this, currentBlock);
    
    // In order for the erase process to run properly, this function must return the current location of
    // the audio engine - basically a sample counter
    // It doesn't need to be exact, but if it is inexact, it should err towards being before the current location
    // rather than after the current location.  The erase head will erase up to, but not including the sample
    // frame returned by this function.  If it is too large a value, sound data that hasn't been played will be 
    // erased.
    
    return currentBlock * blockSize;
}


IOReturn REACAudioEngine::performFormatChange(IOAudioStream *audioStream, const IOAudioStreamFormat *newFormat, const IOAudioSampleRate *newSampleRate)
{     
    if (!duringHardwareInit) {
  //      IOLog("REACAudioEngine[%p]::peformFormatChange(%p, %p, %p)\n", this, audioStream, newFormat, newSampleRate);
    }

    // It is possible that this function will be called with only a format or only a sample rate
    // We need to check for NULL for each of the parameters
    if (newFormat) {
        if (!duringHardwareInit) {
            // #### do we need to make sure output format == input format??
        }
    }
    
    if (newSampleRate) {
        if (!duringHardwareInit) {
            UInt64 newblockTime = blockSize;
            newblockTime *= 1000000000;
            blockTimeoutNS = newblockTime / newSampleRate->whole;
        }
    }
    
    return kIOReturnSuccess;
}


void REACAudioEngine::ourTimerFired(OSObject *target, IOTimerEventSource *sender)
{
    if (target) {
        REACAudioEngine  *audioEngine = OSDynamicCast(REACAudioEngine, target);
        UInt64            thisTimeNS;
        uint64_t          time;
        SInt64            diff;
        
        if (audioEngine) {
            // make sure we have a client, and thus new data so we don't keep on 
            // just looping around the last client's last buffer!    
            IOAudioStream *outStream = audioEngine->getAudioStream(kIOAudioStreamDirectionOutput, 1);
            if (outStream->numClients == 0) {
                // it has, so clean the buffer 
                memset((UInt8*)audioEngine->mThruBuffer, 0, audioEngine->mBufferSize);
            }
                    
            audioEngine->currentBlock++;
            if (audioEngine->currentBlock >= audioEngine->numBlocks) {
                audioEngine->currentBlock = 0;
                audioEngine->takeTimeStamp();
            }
            
            // calculate next time to fire, by taking the time and comparing it to the time we requested.                                 
            clock_get_uptime(&time);
            absolutetime_to_nanoseconds(time, &thisTimeNS);
            // this next calculation must be signed or we will introduce distortion after only a couple of vectors
            diff = ((SInt64)audioEngine->nextTime - (SInt64)thisTimeNS);
            sender->setTimeout(audioEngine->blockTimeoutNS + diff);
            audioEngine->nextTime += audioEngine->blockTimeoutNS;
        }
    }
}

void REACAudioEngine::gotSamples(int numSamples, UInt8 *samples) {
    
}
