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

#include "PCMBlitterLib.h"

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
        if (inSample > 1.0f) {
            inSample = 1.0f;
        } else if (inSample < -1.0f) {
            inSample = -1.0f;
        }
        
        // Scale the -1.0 to 1.0 range to the appropriate scale for signed 16-bit samples and then
        // convert to SInt16 and store in the hardware sample buffer
        if (inSample >= 0) {
            outputBuf[sampleIndex] = (SInt16) (inSample * 32767.0f);
        } else {
            outputBuf[sampleIndex] = (SInt16) (inSample * 32768.0f);
        }
    }
    
    return kIOReturnSuccess;
}

#if 0

IOReturn REACAudioEngine::convertInputSamples(const void *sampleBuf, void *destBuf, UInt32 firstSampleFrame,
                                              UInt32 numSampleFrames, const IOAudioStreamFormat *streamFormat,
                                              IOAudioStream *audioStream) {
    // IOLog("REACAudioEngine::convertInputSamples()\n");
    
    UInt32 numChannels = streamFormat->fNumChannels;
    int resolution = streamFormat->fBitWidth/8;
    float *floatDestBuf = (float *)destBuf;
    
    UInt32 numSamplesLeft = numSampleFrames * numChannels;
    UInt8 *inputBuf = &(((UInt8 *)sampleBuf)[firstSampleFrame * numChannels * resolution]);
    
    bool haveComplained = false; // TODO Debug
    
    // Loop through each sample and scale and convert them
    while (numSamplesLeft > 0) {
        SInt32 inputSample;
        
        if (55 == inputBuf[0] && 55 == inputBuf[1] && 55 == inputBuf[2] && !haveComplained) {
            IOLog("REACAudioEngine::convertInputSamples(): CLIP? (%d, %d)\n", (int) firstSampleFrame, (int) numSampleFrames);
            haveComplained = true;
        }
        
        // Fetch the 24 bit input sample
        inputSample = inputBuf[2];
        inputSample = (inputSample << 8) | inputBuf[1];
        inputSample = (inputSample << 8) | inputBuf[0];

        // Fill the rest with ones if the number is negative (this is required as inputSample is SInt32)
        if (inputSample & 0x800000)
            inputSample |= ~0xffffff;
        
        const float Q = 1.0f / (0x7fffff + 0.5f);
        
        *floatDestBuf = (inputSample + 0.5f) * Q;
        
        inputBuf[0] = 55; // TODO Debug
        inputBuf[1] = 55; // TODO Debug
        inputBuf[2] = 55; // TODO Debug
        
        // Move on to the next sample
        inputBuf += resolution;
        ++floatDestBuf;
        --numSamplesLeft;
    }
    
    return kIOReturnSuccess;
}

#endif



// The function convertInputSamples() is responsible for converting from the hardware format 
// in the input sample buffer to float samples in the destination buffer and scale the samples 
// to a range of -1.0 to 1.0.  This function is guaranteed not to have the samples wrapped
// from the end of the buffer to the beginning.
// This function only needs to be implemented if the device has any input IOAudioStreams
//
// The parameters are as follows:
//		sampleBuf - a pointer to the beginning of the hardware formatted sample buffer - this is the same buffer passed
//					to the IOAudioStream using setSampleBuffer()
//		destBuf - a pointer to the float destination buffer - this is the buffer that the CoreAudio.framework uses
//					its size is numSampleFrames * numChannels * sizeof(float)
//		firstSampleFrame - this is the index of the first sample frame to the input conversion on
//		numSampleFrames - the total number of sample frames to convert and scale
//		streamFormat - the current format of the IOAudioStream this function is operating on
//		audioStream - the audio stream this function is operating on
IOReturn REACAudioEngine::convertInputSamples(const void* inSourceBuffer, void* outTargetBuffer, UInt32 inFirstFrame,
                                              UInt32 inNumberFrames, const IOAudioStreamFormat* inFormat,
                                              IOAudioStream* /*inStream*/) {
	//	figure out what sort of blit we need to do
	if((inFormat->fSampleFormat == kIOAudioStreamSampleFormatLinearPCM) && inFormat->fIsMixable)
	{
		//	it's linear PCM, which means the target is Float32 and we will be calling a blitter, which works in samples not frames
		Float32* theTargetBuffer = (Float32*)outTargetBuffer;
		UInt32 theFirstSample = inFirstFrame * inFormat->fNumChannels;
		UInt32 theNumberSamples = inNumberFrames * inFormat->fNumChannels;
        
		if(inFormat->fNumericRepresentation == kIOAudioStreamNumericRepresentationSignedInt)
		{
			//	it's some kind of signed integer, which we handle as some kind of even byte length
			bool nativeEndianInts;
#if TARGET_RT_BIG_ENDIAN
            nativeEndianInts = (inFormat->fByteOrder == kIOAudioStreamByteOrderBigEndian);
#else
            nativeEndianInts = (inFormat->fByteOrder == kIOAudioStreamByteOrderLittleEndian);
#endif
			
			switch(inFormat->fBitWidth)
			{
				case 8:
                {
                    IOLog("REACAudioEngine::convertInputSamples(): can't handle signed "\
                          "integers with a bit width of 8 at the moment.\n");
                }
					break;
                    
				case 16:
                {
                    SInt16* theSourceBuffer = (SInt16*)inSourceBuffer;
                    if (nativeEndianInts)
                        NativeInt16ToFloat32(&(theSourceBuffer[theFirstSample]), theTargetBuffer, theNumberSamples);
                    else
                        SwapInt16ToFloat32(&(theSourceBuffer[theFirstSample]), theTargetBuffer, theNumberSamples);
                }
					break;
                    
				case 24:
                {
                    UInt8* theSourceBuffer = (UInt8*)inSourceBuffer;
                    if (nativeEndianInts)
                        NativeInt24ToFloat32(&(theSourceBuffer[3*theFirstSample]), theTargetBuffer, theNumberSamples);
                    else
                        SwapInt24ToFloat32(&(theSourceBuffer[3*theFirstSample]), theTargetBuffer, theNumberSamples);
                }
					break;
                    
				case 32:
                {
                    SInt32* theSourceBuffer = (SInt32*)inSourceBuffer;
                    if (nativeEndianInts)
                        NativeInt32ToFloat32(&(theSourceBuffer[theFirstSample]), theTargetBuffer, theNumberSamples);
                    else
                        SwapInt32ToFloat32(&(theSourceBuffer[theFirstSample]), theTargetBuffer, theNumberSamples);
                }
					break;
                    
				default:
					IOLog("REACAudioEngine::convertInputSamples(): can't handle signed integers with a bit width of %d.\n",
                          inFormat->fBitWidth);
					break;
                    
			}
		}
		else if(inFormat->fNumericRepresentation == kIOAudioStreamNumericRepresentationIEEE754Float)
		{
			//	it is some kind of floating point format
#if TARGET_RT_BIG_ENDIAN
			if((inFormat->fBitWidth == 32) && (inFormat->fBitDepth == 32) && (inFormat->fByteOrder == kIOAudioStreamByteOrderBigEndian))
#else
                if((inFormat->fBitWidth == 32) && (inFormat->fBitDepth == 32) && (inFormat->fByteOrder == kIOAudioStreamByteOrderLittleEndian))
#endif
                {
                    //	it's Float32, so we are just going to copy the data
                    Float32* theSourceBuffer = (Float32*)inSourceBuffer;
                    memcpy(theTargetBuffer, &(theSourceBuffer[theFirstSample]), theNumberSamples * sizeof(Float32));
                }
                else
                {
                    IOLog("REACEngine::convertInputSamples(): can't handle floats with a bit width of %d, "\
                          "bit depth of %d, and/or the given byte order.\n",
                          inFormat->fBitWidth, inFormat->fBitDepth);
                }
		}
	}
	else
	{
		//	it's not linear PCM or it's not mixable, so just copy the data into the target buffer
		SInt8* theSourceBuffer = (SInt8*)inSourceBuffer;
		UInt32 theFirstByte = inFirstFrame * (inFormat->fBitWidth / 8) * inFormat->fNumChannels;
		UInt32 theNumberBytes = inNumberFrames * (inFormat->fBitWidth / 8) * inFormat->fNumChannels;
		memcpy(outTargetBuffer, &(theSourceBuffer[theFirstByte]), theNumberBytes);
	}
    
	return kIOReturnSuccess;
}
