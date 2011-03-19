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

#include "REACConnection.h"

// The number of packets to reserve as buffer internally in the driver. Increasing
// this number by one increases the latency by 
// REAC_SAMPLES_PER_PACKET/REAC_SAMPLE_RATE seconds, which is 0.125ms on 96kHz.
// 
// This number determines how resilient the driver should be wrt uneven timing on
// the input network packets: Delays in incoming packets or in the networking stack
// will result in audio dropouts if the delay is bigger than
// BUFFER_OFFSET_FACTOR/REAC_PACKETS_PER_SECOND seconds.
//
// Note that this is only the default value, and is overridden if found in Info.plist
#define BUFFER_OFFSET_FACTOR_DEFAULT   40

// This adjusts the size of the internal audio ring buffers in the driver. It doesn't
// affect latency, it just has to be bigger (in samples) than the CoreAudio float audio
// ring buffer plus a constant buffer offset, yet not be as big as to take unnecessary
// amounts of memory (memory is precious in the kernel).
//
// Note that this is only the default value, and is overridden if found in Info.plist
#define NUM_BLOCKS_DEFAULT             1024

#define super IOAudioEngine

OSDefineMetaClassAndStructors(REACAudioEngine, super)

const SInt32 REACAudioEngine::kVolumeMax = 65535;
const SInt32 REACAudioEngine::kGainMax = 65535;


bool REACAudioEngine::init(REACConnection* proto, OSDictionary *properties) {
    bool result = false;
    OSNumber *number = NULL;
    
    // IOLog("REACAudioEngine[%p]::init()\n", this);
    
    if (NULL == proto) {
        goto Done;
    }
    protocol = proto;
    protocol->retain();

    if (!super::init(properties)) {
        goto Done;
    }
    
    // Do class-specific initialization here
    // If no non-hardware initialization is needed, this function can be removed
    
    number = OSDynamicCast(OSNumber, getProperty(NUM_BLOCKS_KEY));
    numBlocks = (number ? number->unsigned32BitValue() : NUM_BLOCKS_DEFAULT);
    
    number = OSDynamicCast(OSNumber, getProperty(BLOCK_SIZE_KEY));
    blockSize = (number ? number->unsigned32BitValue() : REAC_SAMPLES_PER_PACKET);
    
    number = OSDynamicCast(OSNumber, getProperty(BUFFER_OFFSET_FACTOR_KEY));
    bufferOffsetFactor = (number ? number->unsigned32BitValue() : BUFFER_OFFSET_FACTOR_DEFAULT);
    
    mInBuffer = mOutBuffer = NULL;
    inputStream = outputStream = NULL;
    duringHardwareInit = FALSE;
    mLastValidSampleFrame = 0;
    result = true;
    
Done:
    return result;
}


bool REACAudioEngine::initHardware(IOService *provider) {
    bool result = false;
    IOAudioSampleRate initialSampleRate;
    IOWorkLoop *wl;
    OSString       *desc;
    
    //IOLog("REACAudioEngine[%p]::initHardware(%p)\n", this, provider);
    
    duringHardwareInit = TRUE;
    
    if (!initControls()) {
        goto Done;
    }
    
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
    
    desc = OSDynamicCast(OSString, getProperty(DESCRIPTION_KEY));
    if (desc) {
        setDescription(desc->getCStringNoCopy());
    }
    
    setSampleRate(&initialSampleRate);
    setSampleOffset(blockSize*bufferOffsetFactor);
    setClockIsStable(FALSE);
    
    // Set the number of sample frames in each buffer
    setNumSampleFramesPerBuffer(blockSize * numBlocks);
    
    wl = getWorkLoop();
    if (!wl) {
        goto Done;
    }
            
    result = true;
    
Done:
    duringHardwareInit = FALSE;    
    return result;
}

 
bool REACAudioEngine::createAudioStreams(IOAudioSampleRate *sampleRate) {
    bool            result = false;
    
    UInt32              numInChannels  = protocol->getDeviceInfo()->in_channels;
    UInt32              numOutChannels = protocol->getDeviceInfo()->out_channels;
    UInt32              bufferSizePerChannel;
    OSDictionary       *inFormatDict;
    OSDictionary       *outFormatDict;
    
    IOAudioStreamFormat inFormat;
    IOAudioStreamFormat outFormat;
    
    sampleRate->whole = REAC_SAMPLE_RATE;
    sampleRate->fraction = 0;
    
    inputStream = new IOAudioStream;
    outputStream = new IOAudioStream;
    if (NULL == inputStream || NULL == outputStream) {
        IOLog("REAC: Could not create IOAudioStreams\n");
        goto Error;
    }
        
    if (!inputStream->initWithAudioEngine(this, kIOAudioStreamDirectionInput, 1 /* Starting channel ID */, "REAC Input Stream") ||
        !outputStream->initWithAudioEngine(this, kIOAudioStreamDirectionOutput, 1 /* Starting channel ID */, "REAC Output Stream")) {
        IOLog("REAC: Could not init one of the streams with audio engine. \n");
        goto Error;
    }
    
    inFormatDict = OSDynamicCast(OSDictionary, getProperty(IN_FORMAT_KEY));
    outFormatDict = OSDynamicCast(OSDictionary, getProperty(OUT_FORMAT_KEY));
    if (NULL == inFormatDict || NULL == outFormatDict) {
        IOLog("REAC: inFormatDict or outFormatDict is NULL\n");
        goto Error;
    }
    
    if (IOAudioStream::createFormatFromDictionary(inFormatDict, &inFormat) == NULL ||
        IOAudioStream::createFormatFromDictionary(outFormatDict, &outFormat) == NULL) {
        IOLog("REAC: Error in createFormatFromDictionary()\n");
        goto Error;
    }
    
    inFormat.fNumChannels = numInChannels;
    outFormat.fNumChannels = numOutChannels;
    
    inFormat.fBitDepth = REAC_RESOLUTION * 8;
    outFormat.fBitDepth = REAC_RESOLUTION * 8;
    
    inputStream->addAvailableFormat(&inFormat, sampleRate, sampleRate);
    outputStream->addAvailableFormat(&outFormat, sampleRate, sampleRate);
    
    inputStream->setFormat(&inFormat);
    outputStream->setFormat(&outFormat);
    
    bufferSizePerChannel = blockSize * numBlocks * REAC_RESOLUTION;
    mInBufferSize = bufferSizePerChannel * numInChannels;
    mOutBufferSize = bufferSizePerChannel * numOutChannels;
    
    if (mInBuffer == NULL) {
        mInBuffer = (void *)IOMalloc(mInBufferSize);
        if (NULL == mInBuffer) {
            IOLog("REAC: Error allocating input buffer - %d bytes.\n", (int) mInBufferSize);
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
    
    inputStream->setSampleBuffer(mInBuffer, mInBufferSize);
    addAudioStream(inputStream);
    inputStream->release();
    
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
    
    if (NULL != protocol) {
        protocol->release();
    }
    
    if (NULL != mInBuffer) {
        IOFree(mInBuffer, mInBufferSize);
        mInBuffer = NULL;
    }
    if (NULL != mOutBuffer) {
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
    
    return kIOReturnSuccess;
}

IOReturn REACAudioEngine::performAudioEngineStop() {
    //IOLog("REACAudioEngine[%p]::performAudioEngineStop()\n", this);
    
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
        // IOLog("REACAudioEngine[%p]::peformFormatChange(%p, %p, %p)\n", this, audioStream, newFormat, newSampleRate);
    }

    // It is possible that this function will be called with only a format or only a sample rate
    // We need to check for NULL for each of the parameters
    if (NULL != newFormat) {
    }
    
    if (NULL != newSampleRate) {
    }
    
    return kIOReturnSuccess;
}

void REACAudioEngine::gotSamples(UInt8 **data, UInt32 *bufferSize) {
    if (NULL == mInBuffer) {
        // This should never happen. But better complain than crash the computer I guess
        IOLog("REACAudioEngine::gotSamples(): Internal error.\n");
        return;
    }
    
    if (inputStream->format.fNumChannels != protocol->getDeviceInfo()->in_channels ||
        inputStream->format.fBitWidth != REAC_RESOLUTION*8) {
        IOLog("REACAudioEngine::gotSamples(): Invalid input stream format.\n");
        return;
    }
    
    const int bytesPerSample = inputStream->format.fBitWidth/8 * inputStream->format.fNumChannels;
    const int bytesPerPacket = bytesPerSample * REAC_SAMPLES_PER_PACKET;
    
    *data = (UInt8 *)mInBuffer + currentBlock*blockSize*bytesPerSample;
    *bufferSize = bytesPerPacket;
    
    if (REACConnection::REAC_MASTER != protocol->getMode()) {
        incrementBlockCounter();
    }
}

void REACAudioEngine::getSamples(UInt8 **data, UInt32 *bufferSize) {
    const int bytesPerSample = outputStream->format.fBitWidth/8 * outputStream->format.fNumChannels;
    const int bytesPerPacket = bytesPerSample * REAC_SAMPLES_PER_PACKET;

    *data = (UInt8 *)mOutBuffer + currentBlock*blockSize*bytesPerSample;
    *bufferSize = bytesPerPacket;
    
    if (REACConnection::REAC_MASTER == protocol->getMode()) {
        incrementBlockCounter();
    }
    return;
}

void REACAudioEngine::incrementBlockCounter() {
    currentBlock++;
    if (currentBlock >= numBlocks) {
        currentBlock = 0;
        takeTimeStamp();
    }
}



#define addControl(control, handler) \
    if (!control) { \
        IOLog("REACAudioEngine::initControls(): Failed to add control.\n"); \
        goto Done; \
    } \
    control->setValueChangeHandler(handler, this); \
    addDefaultAudioControl(control); \
    control->release();

bool REACAudioEngine::initControls() {
    const char *channelNameMap[17] = {
        kIOAudioControlChannelNameAll,
        kIOAudioControlChannelNameLeft,
        kIOAudioControlChannelNameRight,
        kIOAudioControlChannelNameCenter,
        kIOAudioControlChannelNameLeftRear,
        kIOAudioControlChannelNameRightRear,
        kIOAudioControlChannelNameSub
    };
    
    bool               result = false;
    IOAudioControl    *control = NULL;
    
    for (UInt32 channel=0; channel <= 16; channel++) {
        mVolume[channel] = mGain[channel] = 65535;
        mMuteOut[channel] = mMuteIn[channel] = false;
    }
    
    for (UInt32 channel=7; channel <= 16; channel++)
        channelNameMap[channel] = "Unknown Channel";
    
    for (unsigned channel=0; channel <= 16; channel++) {
        
        // Create an output volume control for each channel with an int range from 0 to 65535
        // and a db range from -72 to 0
        // Once each control is added to the audio engine, they should be released
        // so that when the audio engine is done with them, they get freed properly
        control = IOAudioLevelControl::createVolumeControl(REACAudioEngine::kVolumeMax,         // Initial value
                                                           0,                                   // min value
                                                           REACAudioEngine::kVolumeMax,         // max value
                                                           (-72 << 16) + (32768),               // -72 in IOFixed (16.16)
                                                           0,                                   // max 0.0 in IOFixed
                                                           channel,                             // kIOAudioControlChannelIDDefaultLeft,
                                                           channelNameMap[channel],             // kIOAudioControlChannelNameLeft,
                                                           channel,                             // control ID - driver-defined
                                                           kIOAudioControlUsageOutput);
        addControl(control, (IOAudioControl::IntValueChangeHandler)volumeChangeHandler);
        
        // Gain control for each channel
        control = IOAudioLevelControl::createVolumeControl(REACAudioEngine::kGainMax,           // Initial value
                                                           0,                                   // min value
                                                           REACAudioEngine::kGainMax,           // max value
                                                           0,                                   // min 0.0 in IOFixed
                                                           (72 << 16) + (32768),                // 72 in IOFixed (16.16)
                                                           channel,                             // kIOAudioControlChannelIDDefaultLeft,
                                                           channelNameMap[channel],             // kIOAudioControlChannelNameLeft,
                                                           channel,                             // control ID - driver-defined
                                                           kIOAudioControlUsageInput);
        addControl(control, (IOAudioControl::IntValueChangeHandler)gainChangeHandler);
    }
    
    // Create an output mute control
    control = IOAudioToggleControl::createMuteControl(false,                                    // initial state - unmuted
                                                      kIOAudioControlChannelIDAll,              // Affects all channels
                                                      kIOAudioControlChannelNameAll,
                                                      0,                                        // control ID - driver-defined
                                                      kIOAudioControlUsageOutput);
    addControl(control, (IOAudioControl::IntValueChangeHandler)outputMuteChangeHandler);
    
    // Create an input mute control
    control = IOAudioToggleControl::createMuteControl(false,                                    // initial state - unmuted
                                                      kIOAudioControlChannelIDAll,              // Affects all channels
                                                      kIOAudioControlChannelNameAll,
                                                      0,                                        // control ID - driver-defined
                                                      kIOAudioControlUsageInput);
    addControl(control, (IOAudioControl::IntValueChangeHandler)inputMuteChangeHandler);
    
    result = true;
    
Done:
    return result;
}


IOReturn REACAudioEngine::volumeChangeHandler(IOService *target, IOAudioControl *volumeControl, SInt32 oldValue, SInt32 newValue) {
    IOReturn            result = kIOReturnBadArgument;
    REACAudioEngine    *audioEngine = (REACAudioEngine *)target;
    
    if (audioEngine)
        result = audioEngine->volumeChanged(volumeControl, oldValue, newValue);
    return result;
}


IOReturn REACAudioEngine::volumeChanged(IOAudioControl *volumeControl, SInt32 oldValue, SInt32 newValue) {
    if (volumeControl)
        mVolume[volumeControl->getChannelID()] = newValue;
    return kIOReturnSuccess;
}


IOReturn REACAudioEngine::outputMuteChangeHandler(IOService *target, IOAudioControl *muteControl, SInt32 oldValue, SInt32 newValue) {
    IOReturn          result = kIOReturnBadArgument;
    REACAudioEngine  *audioEngine = (REACAudioEngine *)target;
    
    if (audioEngine)
        result = audioEngine->outputMuteChanged(muteControl, oldValue, newValue);
    return result;
}


IOReturn REACAudioEngine::outputMuteChanged(IOAudioControl *muteControl, SInt32 oldValue, SInt32 newValue) {
    if (muteControl)
        mMuteOut[muteControl->getChannelID()] = newValue;
    return kIOReturnSuccess;
}


IOReturn REACAudioEngine::gainChangeHandler(IOService *target, IOAudioControl *gainControl, SInt32 oldValue, SInt32 newValue) {
    IOReturn            result = kIOReturnBadArgument;
    REACAudioEngine    *audioEngine = (REACAudioEngine *)target;
    
    if (audioEngine)
        result = audioEngine->gainChanged(gainControl, oldValue, newValue);
    return result;
}


IOReturn REACAudioEngine::gainChanged(IOAudioControl *gainControl, SInt32 oldValue, SInt32 newValue) {
    if (gainControl)
        mGain[gainControl->getChannelID()] = newValue;
    return kIOReturnSuccess;
}


IOReturn REACAudioEngine::inputMuteChangeHandler(IOService *target, IOAudioControl *muteControl, SInt32 oldValue, SInt32 newValue) {
    IOReturn            result = kIOReturnBadArgument;
    REACAudioEngine    *audioEngine = (REACAudioEngine*)target;
    
    if (audioEngine)
        result = audioEngine->inputMuteChanged(muteControl, oldValue, newValue);
    return result;
}


IOReturn REACAudioEngine::inputMuteChanged(IOAudioControl *muteControl, SInt32 oldValue, SInt32 newValue) {
    if (muteControl)
        mMuteIn[muteControl->getChannelID()] = newValue;
    return kIOReturnSuccess;
}
