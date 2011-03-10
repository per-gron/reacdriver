/*
 *  REACAudioClip.cpp
 *  REAC
 *
 *  Created by Per Eckerdal on 02/01/2011.
 *  Copyright 2011 Per Eckerdal. All rights reserved.
 *
 */

#include "REACAudioEngine.h"

#include <IOKit/IOLib.h>

// The function clipOutputSamples() is called to clip and convert samples from the float mix buffer into the actual
// hardware sample buffer.  The samples to be clipped, are guaranteed not to wrap from the end of the buffer to the
// beginning.
// This implementation is very inefficient, but illustrates the clip and conversion process that must take place.
// Each floating-point sample must be clipped to a range of -1.0 to 1.0 and then converted to the hardware buffer
// format

// The parameters are as follows:
//		mixBuf - a pointer to the beginning of the float mix buffer - its size is based on the number of sample frames
// 					times the number of channels for the stream
//		sampleBuf - a pointer to the beginning of the hardware formatted sample buffer - this is the same buffer passed
//					to the IOAudioStream using setSampleBuffer()
//		firstSampleFrame - this is the index of the first sample frame to perform the clipping and conversion on
//		numSampleFrames - the total number of sample frames to clip and convert
//		streamFormat - the current format of the IOAudioStream this function is operating on
//		audioStream - the audio stream this function is operating on
IOReturn REACAudioEngine::clipOutputSamples(const void *mixBuf, void *sampleBuf, UInt32 firstSampleFrame,
                                            UInt32 numSampleFrames, const IOAudioStreamFormat *streamFormat,
                                            IOAudioStream *audioStream) {
    UInt32 sampleIndex, maxSampleIndex;
    float *floatMixBuf;
    SInt16 *outputBuf;
    
    // Start by casting the void * mix and sample buffers to the appropriate types - float * for the mix buffer
    // and SInt16 * for the sample buffer (because our sample hardware uses signed 16-bit samples)
    floatMixBuf = (float *)mixBuf;
    outputBuf = (SInt16 *)sampleBuf;
    
    // We calculate the maximum sample index we are going to clip and convert
    // This is an index into the entire sample and mix buffers
    maxSampleIndex = (firstSampleFrame + numSampleFrames) * streamFormat->fNumChannels;
    
    // Loop through the mix/sample buffers one sample at a time and perform the clip and conversion operations
    for (sampleIndex = (firstSampleFrame * streamFormat->fNumChannels); sampleIndex < maxSampleIndex; sampleIndex++) {
        float inSample;
        
        // Fetch the floating point mix sample
        inSample = floatMixBuf[sampleIndex];
        
        // Clip that sample to a range of -1.0 to 1.0
        // A softer clipping operation could be done here
        if (inSample > 1.0) {
            inSample = 1.0;
        } else if (inSample < -1.0) {
            inSample = -1.0;
        }
        
        // Scale the -1.0 to 1.0 range to the appropriate scale for signed 16-bit samples and then
        // convert to SInt16 and store in the hardware sample buffer
        if (inSample >= 0) {
            outputBuf[sampleIndex] = (SInt16) (inSample * 32767.0);
        } else {
            outputBuf[sampleIndex] = (SInt16) (inSample * 32768.0);
        }
    }
    
    return kIOReturnSuccess;
}

// The function convertInputSamples() is responsible for converting from the hardware format 
// in the input sample buffer to float samples in the destination buffer and scale the samples 
// to a range of -1.0 to 1.0.  This function is guaranteed not to have the samples wrapped
// from the end of the buffer to the beginning.
// This function only needs to be implemented if the device has any input IOAudioStreams

// This implementation is very inefficient, but illustrates the conversion and scaling that needs to take place.

// The parameters are as follows:
//		sampleBuf - a pointer to the beginning of the hardware formatted sample buffer - this is the same buffer passed
//					to the IOAudioStream using setSampleBuffer()
//		destBuf - a pointer to the float destination buffer - this is the buffer that the CoreAudio.framework uses
//					its size is numSampleFrames * numChannels * sizeof(float)
//		firstSampleFrame - this is the index of the first sample frame to the input conversion on
//		numSampleFrames - the total number of sample frames to convert and scale
//		streamFormat - the current format of the IOAudioStream this function is operating on
//		audioStream - the audio stream this function is operating on
IOReturn REACAudioEngine::convertInputSamples(const void *sampleBuf, void *destBuf, UInt32 firstSampleFrame,
                                              UInt32 numSampleFrames, const IOAudioStreamFormat *streamFormat,
                                              IOAudioStream *audioStream) {
    // IOLog("REACAudioEngine::convertInputSamples()\n");
    
    UInt32 numSamplesLeft;
    float *floatDestBuf;
    SInt16 *inputBuf;
    
    // Start by casting the destination buffer to a float *
    floatDestBuf = (float *)destBuf;
    // Determine the starting point for our input conversion 
    inputBuf = &(((SInt16 *)sampleBuf)[firstSampleFrame * streamFormat->fNumChannels]);
    
    // Calculate the number of actual samples to convert
    numSamplesLeft = numSampleFrames * streamFormat->fNumChannels;
    
    // Loop through each sample and scale and convert them
    while (numSamplesLeft > 0) {
        SInt16 inputSample;
        
        // Fetch the SInt16 input sample
        inputSample = *inputBuf;
        
        // Scale that sample to a range of -1.0 to 1.0, convert to float and store in the destination buffer
        // at the proper location
        if (inputSample >= 0) {
            *floatDestBuf = inputSample / 32767.0;
        } else {
            *floatDestBuf = inputSample / 32768.0;
        }
        
        // Move on to the next sample
        ++inputBuf;
        ++floatDestBuf;
        --numSamplesLeft;
    }
    
    return kIOReturnSuccess;
}









#if 0
IOReturn REACAudioEngine::clipOutputSamples(const void *mixBuf, void *sampleBuf, UInt32 firstSampleFrame, UInt32 numSampleFrames, const IOAudioStreamFormat *streamFormat, IOAudioStream *audioStream)
{
    UInt32                channelCount = streamFormat->fNumChannels;
    UInt32                offset = firstSampleFrame * channelCount;
    UInt32                byteOffset = offset * sizeof(float);
    UInt32                numBytes = numSampleFrames * channelCount * sizeof(float);
    REACDevice*    device = (REACDevice*)audioDevice;
    
#if 0
    IOLog("REACAudioEngine[%p]::clipOutputSamples() -- channelCount:%u \n", this, (uint)channelCount);
    IOLog("    input -- numChannels: %u", (uint)inputStream->format.fNumChannels);
    IOLog("    bitDepth: %u", (uint)inputStream->format.fBitDepth);
    IOLog("    bitWidth: %u", (uint)inputStream->format.fBitWidth);
    IOLog("    \n");
    IOLog("    output -- numChannels: %u", (uint)inputStream->format.fNumChannels);
    IOLog("    bitDepth: %u", (uint)inputStream->format.fBitDepth);
    IOLog("    bitWidth: %u", (uint)inputStream->format.fBitWidth);
    IOLog("    \n");
#endif
    
#if 0
    IOLog("INPUT: firstSampleFrame: %u   numSampleFrames: %u \n", (uint)firstSampleFrame, (uint)numSampleFrames);
#endif
    mLastValidSampleFrame = firstSampleFrame+numSampleFrames;
    
    // TODO: where is the sampleFrame wrapped?
    // TODO: try to put a mutex around reading and writing
    // TODO: why is the reading always trailing by at least 512 frames? (when 512 is the input framesize)?
    
    if (device->mMuteIn[0]) {
        memset((UInt8*)mThruBuffer + byteOffset, 0, numBytes);
    }
    else {
        memcpy((UInt8*)mThruBuffer + byteOffset, (UInt8 *)mixBuf + byteOffset, numBytes);
        
        float masterGain = device->mGain[0] / ((float)REACDevice::kGainMax);
        float masterVolume = device->mVolume[0] / ((float)REACDevice::kVolumeMax);
        
        for (UInt32 channel = 0; channel < channelCount; channel++) {
            SInt32    channelMute = device->mMuteIn[channel+1];
            float    channelGain = device->mGain[channel+1] / ((float)REACDevice::kGainMax);
            float    channelVolume = device->mVolume[channel+1] / ((float)REACDevice::kVolumeMax);
            float    adjustment = masterVolume * channelVolume * masterGain * channelGain;
            
            for (UInt32 channelBufferIterator = 0; channelBufferIterator < numSampleFrames; channelBufferIterator++) {
                if (channelMute)
                    mThruBuffer[offset + channelBufferIterator*channelCount + channel] = 0;
                else
                    mThruBuffer[offset + channelBufferIterator*channelCount + channel] *= adjustment;
            }
        }
    }
    return kIOReturnSuccess;
}


// This is called when client apps need input audio.  Here we give them saved audio from the clip routine.

IOReturn REACAudioEngine::convertInputSamples(const void *sampleBuf, void *destBuf, UInt32 firstSampleFrame, UInt32 numSampleFrames, const IOAudioStreamFormat *streamFormat, IOAudioStream *audioStream)
{
    UInt32                frameSize = streamFormat->fNumChannels * sizeof(float);
    UInt32                offset = firstSampleFrame * frameSize;
    REACDevice*    device = (REACDevice*)audioDevice;
    
#if 0
    //IOLog("REACAudioEngine[%p]::convertInputSamples() -- channelCount:%u \n", this, (uint)streamFormat->fNumChannels);
    IOLog("OUTPUT: firstSampleFrame: %u   numSampleFrames: %u \n", (uint)firstSampleFrame, (uint)numSampleFrames);
    IOLog("    mLastValidSampleFrame: %u  (diff: %ld)   \n", (uint)mLastValidSampleFrame, long(mLastValidSampleFrame) - long(firstSampleFrame+numSampleFrames));
#endif 
    
    if (device->mMuteOut[0])
        memset((UInt8*)destBuf, 0, numSampleFrames * frameSize);
    else
        memcpy((UInt8*)destBuf, (UInt8*)mThruBuffer + offset, numSampleFrames * frameSize);
    
    return kIOReturnSuccess;
}
#endif



#if 0

void got_packet(u_char *args, const struct pcap_pkthdr *header, const u_char *packet) {
    for (int i=0; i<record_channels; i++) {  
        for (int j=0; j<REAC_SAMPLES_PER_PACKET; j++) {
            if (0 == i%2) {
                buf[i][buffer_place+j*REAC_RESOLUTION  ] = reac->samples[j][i/2][3];
                buf[i][buffer_place+j*REAC_RESOLUTION+1] = reac->samples[j][i/2][0];
                buf[i][buffer_place+j*REAC_RESOLUTION+2] = reac->samples[j][i/2][1];
            }
            else {
                buf[i][buffer_place+j*REAC_RESOLUTION  ] = reac->samples[j][i/2][4];
                buf[i][buffer_place+j*REAC_RESOLUTION+1] = reac->samples[j][i/2][5];
                buf[i][buffer_place+j*REAC_RESOLUTION+2] = reac->samples[j][i/2][2];
            }
        }
    }
}


#endif // #if 0
