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

#define super IOAudioEngine

OSDefineMetaClassAndStructors(REACAudioEngine, IOAudioEngine)

const SInt32 REACAudioEngine::kVolumeMax = 65535;
const SInt32 REACAudioEngine::kGainMax = 65535;


bool REACAudioEngine::init(REACProtocol* proto, OSDictionary *properties) {
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


bool REACAudioEngine::initHardware(IOService *provider) {
    return false; // TODO Debug
    
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

    if (!createAudioStreams(&initialSampleRate) ||
        initialSampleRate.whole == 0) {
        IOLog("REACAudioEngine::initHardware() failed\n");
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
    
    // FIXME for REAC_MASTER: timerEventSource = IOTimerEventSource::timerEventSource(this, ourTimerFired);
    
    // FIXME for REAC_MASTER: if (!timerEventSource) {
    // FIXME for REAC_MASTER:     goto Done;
    // FIXME for REAC_MASTER: }
    
    // FIXME for REAC_MASTER: workLoop->addEventSource(timerEventSource);
        
    result = true;
    
Done:
    duringHardwareInit = FALSE;    
    return result;
}

 
bool REACAudioEngine::createAudioStreams(IOAudioSampleRate *sampleRate) {
    return false; // TODO Debug
    
    bool            result = false;
    OSString       *desc;
    
    UInt32              numInChannels = protocol->getDeviceInfo()->in_channels;
    UInt32              numOutChannels = protocol->getDeviceInfo()->in_channels;
    UInt32              bufferSizePerChannel;
    OSDictionary       *formatDict;
    
    IOAudioStreamFormat inFormat;
    IOAudioStreamFormat outFormat;
    
    desc = OSDynamicCast(OSString, getProperty(DESCRIPTION_KEY));
    if (desc) {
        setDescription(desc->getCStringNoCopy());
    }
    
    formatDict = OSDynamicCast(OSDictionary, getProperty(FORMAT_KEY));
    if (NULL == formatDict) {
        IOLog("REAC: formatDict is NULL\n");
        goto Error;
    }
    
    inputStream = new IOAudioStream;
    if (inputStream == NULL) {
        IOLog("REAC: Could not create new input IOAudioStream\n");
        goto Error;
    }
    
    outputStream = new IOAudioStream;
    if (outputStream == NULL) {
        IOLog("REAC: Could not create new output IOAudioStream\n");
        goto Error;
    }
    
    if (!inputStream->initWithAudioEngine(this, kIOAudioStreamDirectionInput, 1 /* Starting channel ID */, "REAC Input Stream") ||
        !outputStream->initWithAudioEngine(this, kIOAudioStreamDirectionOutput, 1 /* Starting channel ID */, "REAC Output Stream")) {
        IOLog("REAC: Could not init one of the streams with audio engine. \n");
        goto Error;
    }
    
    if (IOAudioStream::createFormatFromDictionary(formatDict, &inFormat) == NULL ||
        IOAudioStream::createFormatFromDictionary(formatDict, &outFormat) == NULL) {
        IOLog("REAC: Error in createFormatFromDictionary()\n");
        goto Error;
    }
    
    inFormat.fBitWidth = REAC_RESOLUTION * 8;
    outFormat.fBitWidth = REAC_RESOLUTION * 8;
    
    sampleRate->whole = REAC_SAMPLE_RATE;
    sampleRate->fraction = 0;
    
    inputStream->addAvailableFormat(&inFormat, sampleRate, sampleRate);
    outputStream->addAvailableFormat(&outFormat, sampleRate, sampleRate);
    
    bufferSizePerChannel = blockSize * numBlocks * REAC_RESOLUTION;
    mInBufferSize = bufferSizePerChannel * numInChannels;
    mOutBufferSize = bufferSizePerChannel * numOutChannels;
    
    if (mInBuffer == NULL) {
        mInBuffer = (void *)IOMalloc(mInBufferSize);
        if (NULL == mInBuffer) {
            IOLog("REAC: Error allocating input buffer - %lu bytes.\n", (unsigned long)mInBufferSize);
            goto Error;
        }
    }
    if (mOutBuffer == NULL) {
        mOutBuffer = (void *)IOMalloc(mOutBufferSize);
        if (NULL == mOutBuffer) {
            IOLog("REAC: Error allocating output buffer - %lu bytes.\n", (unsigned long)mOutBufferSize);
            goto Error;
        }
    }
    
    inputStream->setFormat(&inFormat);
    inputStream->setSampleBuffer(mInBuffer, mInBufferSize);
    addAudioStream(inputStream);
    inputStream->release();
    
    outputStream->setFormat(&outFormat);
    outputStream->setSampleBuffer(mOutBuffer, mOutBufferSize);       
    addAudioStream(outputStream);
    outputStream->release();
    
    result = true;
    goto Done;

Error:
    IOLog("REACAudioEngine[%p]::createAudioStreams() - ERROR\n", this);

    if (inputStream)
        inputStream->release();
    if (outputStream)
        outputStream->release();
    
Done:
    if (!result)
        IOLog("REACAudioEngine[%p]::createAudioStreams() - failed!\n", this);
    return result;
}

 
void REACAudioEngine::free() {
    //IOLog("REACAudioEngine[%p]::free()\n", this);
    
    if (mInBuffer) {
        IOFree(mInBuffer, mInBufferSize);
        mInBuffer = NULL;
    }
    if (mOutBuffer) {
        IOFree(mOutBuffer, mOutBufferSize);
        mOutBuffer = NULL;
    }
    super::free();
}

IOReturn REACAudioEngine::performAudioEngineStart() {
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
    
    // FIXME for REAC_MASTER: timerEventSource->setTimeout(blockTimeoutNS);
    
    // FIXME for REAC_MASTER: uint64_t time;
    // FIXME for REAC_MASTER: clock_get_uptime(&time);
    // FIXME for REAC_MASTER: absolutetime_to_nanoseconds(time, &nextTime);

    // FIXME for REAC_MASTER: nextTime += blockTimeoutNS;
    
    return kIOReturnSuccess;
}

IOReturn REACAudioEngine::performAudioEngineStop() {
    //IOLog("REACAudioEngine[%p]::performAudioEngineStop()\n", this);
         
    // FIXME for REAC_MASTER: timerEventSource->cancelTimeout();
    
    return kIOReturnSuccess;
}

UInt32 REACAudioEngine::getCurrentSampleFrame() {
    //IOLog("REACAudioEngine[%p]::getCurrentSampleFrame() - currentBlock = %lu\n", this, currentBlock);
    
    // In order for the erase process to run properly, this function must return the current location of
    // the audio engine - basically a sample counter
    // It doesn't need to be exact, but if it is inexact, it should err towards being before the current location
    // rather than after the current location.  The erase head will erase up to, but not including the sample
    // frame returned by this function.  If it is too large a value, sound data that hasn't been played will be 
    // erased.
    
    return currentBlock * blockSize;
}


IOReturn REACAudioEngine::performFormatChange(IOAudioStream *audioStream, const IOAudioStreamFormat *newFormat,
                                              const IOAudioSampleRate *newSampleRate) {
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
                // memset((UInt8*)audioEngine->mThruBuffer, 0, audioEngine->mBufferSize);
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
    if (NULL == mInBuffer) {
        // This should never happen. But better complain than crash the computer I guess
        IOLog("REACAudioEngine::gotSamples(): Internal error.");
        return;
    }
    
    int numChannels = inputStream->format.fNumChannels;
    int bytesPerSample = inputStream->format.fBitWidth/8 * numChannels;
    int resolution = inputStream->format.fBitWidth/8;
    UInt8 *inBuffer = (UInt8*)mInBuffer + currentBlock*blockSize*bytesPerSample;

    // TODO FIXME Hardcode limit to 16 channels:
    int actualNumChannels = protocol->getDeviceInfo()->in_channels;
    memset(inBuffer, 0, numSamples*bytesPerSample);
    
    for (int i=0; i<numSamples; i++) {
        for (int j=0; j<actualNumChannels; j++) {
            int offset = bytesPerSample*i + resolution*(j/2);
            UInt8 *currentInBuf = inBuffer + bytesPerSample*i + resolution*j;
            if (0 == j%2) {
                currentInBuf[0] = samples[offset+3];
                currentInBuf[1] = samples[offset+0];
                currentInBuf[2] = samples[offset+1];
            }
            else {
                currentInBuf[0] = samples[offset+4];
                currentInBuf[1] = samples[offset+5];
                currentInBuf[2] = samples[offset+2];
            }
        }
    }
    
    switch (protocol->getMode()) {
        case REACProtocol::REAC_SPLIT:
            break;
            
        case REACProtocol::REAC_MASTER:
        case REACProtocol::REAC_SLAVE:
        default:
            IOLog("REACAudioEngine::gotSamples(): Unsupported REAC mode\n");
            break;
    }
    
    currentBlock++;
    if (currentBlock >= numBlocks) {
        currentBlock = 0;
        takeTimeStamp();
    }
}
