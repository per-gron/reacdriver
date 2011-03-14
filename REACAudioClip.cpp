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
//
// The parameters are as follows:
//		mixBuf - a pointer to the beginning of the float mix buffer - its size is based on the number of sample frames
// 					times the number of channels for the stream
//		sampleBuf - a pointer to the beginning of the hardware formatted sample buffer - this is the same buffer passed
//					to the IOAudioStream using setSampleBuffer()
//		firstSampleFrame - this is the index of the first sample frame to perform the clipping and conversion on
//		numSampleFrames - the total number of sample frames to clip and convert
//		streamFormat - the current format of the IOAudioStream this function is operating on
//		audioStream - the audio stream this function is operating on
IOReturn REACAudioEngine::clipOutputSamples(const void* inMixBuffer, void* destBuf, UInt32 firstSampleFrame, UInt32 numSampleFrames, const IOAudioStreamFormat* streamFormat, IOAudioStream* /*audioStream*/)
{
	//	figure out what sort of blit we need to do
	if((streamFormat->fSampleFormat == kIOAudioStreamSampleFormatLinearPCM) && streamFormat->fIsMixable)
	{
		//	it's mixable linear PCM, which means we will be calling a blitter, which works in samples not frames
		Float32* theMixBuffer = (Float32*)inMixBuffer;
		UInt32 theFirstSample = firstSampleFrame * streamFormat->fNumChannels;
		UInt32 theNumberSamples = numSampleFrames * streamFormat->fNumChannels;
        
		if(streamFormat->fNumericRepresentation == kIOAudioStreamNumericRepresentationSignedInt)
		{
			//	it's some kind of signed integer, which we handle as some kind of even byte length
			bool nativeEndianInts;
#if TARGET_RT_BIG_ENDIAN
            nativeEndianInts = (streamFormat->fByteOrder == kIOAudioStreamByteOrderBigEndian);
#else
            nativeEndianInts = (streamFormat->fByteOrder == kIOAudioStreamByteOrderLittleEndian);
#endif
			
			switch(streamFormat->fBitWidth)
			{
				case 8:
                {
                    IOLog("REACAudioEngine::clipOutputSamples(): Can't handle signed integers "\
                          "with a bit width of 8 at the moment.\n");
                }
					break;
                    
				case 16:
                {
                    SInt16* theTargetBuffer = (SInt16*)destBuf;
                    if (nativeEndianInts)
                        Float32ToNativeInt16(&(theMixBuffer[theFirstSample]), &(theTargetBuffer[theFirstSample]), theNumberSamples);
                    else
                        Float32ToSwapInt16(&(theMixBuffer[theFirstSample]), &(theTargetBuffer[theFirstSample]), theNumberSamples);
                }
					break;
                    
				case 24:
                {
                    UInt8* theTargetBuffer = (UInt8*)destBuf;
                    if (nativeEndianInts)
                        Float32ToNativeInt24(&(theMixBuffer[theFirstSample]), &(theTargetBuffer[3*theFirstSample]), theNumberSamples);
                    else
                        Float32ToSwapInt24(&(theMixBuffer[theFirstSample]), &(theTargetBuffer[3*theFirstSample]), theNumberSamples);
                }
					break;
                    
				case 32:
                {
                    SInt32* theTargetBuffer = (SInt32*)destBuf;
                    if (nativeEndianInts)
                        Float32ToNativeInt32(&(theMixBuffer[theFirstSample]), &(theTargetBuffer[theFirstSample]), theNumberSamples);
                    else
                        Float32ToSwapInt32(&(theMixBuffer[theFirstSample]), &(theTargetBuffer[theFirstSample]), theNumberSamples);
                }
					break;
                    
				default:
					IOLog("REACAudioEngine::clipOutputSamples(): Can't handle signed integers "\
                          "with a bit width of %d.\n", streamFormat->fBitWidth);
					break;
                    
			}
		}
		else if(streamFormat->fNumericRepresentation == kIOAudioStreamNumericRepresentationIEEE754Float)
		{
			//	it is some kind of floating point format
#if TARGET_RT_BIG_ENDIAN
			if((streamFormat->fBitWidth == 32) && (streamFormat->fBitDepth == 32) && (streamFormat->fByteOrder == kIOAudioStreamByteOrderBigEndian))
#else
                if((streamFormat->fBitWidth == 32) && (streamFormat->fBitDepth == 32) && (streamFormat->fByteOrder == kIOAudioStreamByteOrderLittleEndian))
#endif
                {
                    //	it's Float32, so we are just going to copy the data
                    Float32* theTargetBuffer = (Float32*)destBuf;
                    memcpy(&(theTargetBuffer[theFirstSample]), &(theMixBuffer[theFirstSample]), theNumberSamples * sizeof(Float32));
                }
                else
                {
                    IOLog("REACAudioEngine::clipOutputSamples(): Can't handle floats with a bit width "\
                          "of %d, bit depth of %d, and/or the given byte order.\n", streamFormat->fBitWidth,
                          streamFormat->fBitDepth);
                }
		}
	}
	else
	{
		//	it's not linear PCM or it's not mixable, so just copy the data into the target buffer
		SInt8* theMixBuffer = (SInt8*)inMixBuffer;
		SInt8* theTargetBuffer = (SInt8*)destBuf;
		UInt32 theFirstByte = firstSampleFrame * (streamFormat->fBitWidth / 8) * streamFormat->fNumChannels;
		UInt32 theNumberBytes = numSampleFrames * (streamFormat->fBitWidth / 8) * streamFormat->fNumChannels;
		memcpy(&(theTargetBuffer[theFirstByte]), &(theMixBuffer[theFirstByte]), theNumberBytes);
	}
    
	return kIOReturnSuccess;
}

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
IOReturn REACAudioEngine::convertInputSamples(const void* sampleBuf, void* destBuf, UInt32 firstSampleFrame,
                                              UInt32 numSampleFrames, const IOAudioStreamFormat* streamFormat,
                                              IOAudioStream* /*audioStream*/) {
    { // Check if we'll have an audio drop out, and log if that's the case.
        const int numChannels = inputStream->format.fNumChannels;
        const int resolution = inputStream->format.fBitWidth/8;
        const int bytesPerSample = resolution * numChannels;
        
        // This is the place in the buffer where we're currently receiving data from the network
        UInt8 *inBufferPosition = (UInt8 *)mInBuffer + currentBlock*blockSize*bytesPerSample;
        // Pointers to where we'll begin and stop writing
        UInt8 *bufferBeginWritePosition = ((UInt8 *)sampleBuf) + firstSampleFrame*bytesPerSample;
        UInt8 *bufferStopWritePosition = bufferBeginWritePosition + numSampleFrames*bytesPerSample;
        
        // Check if we're going to cross inBufferPosition (this leads to audio dropouts)
        if (inBufferPosition >= bufferBeginWritePosition && inBufferPosition < bufferStopWritePosition) {
            IOLog("REACAudioEngine::convertInputSamples(): Audio drop-out! (by %d samples, when converting %d samples)\n",
                  (int) (firstSampleFrame+numSampleFrames - currentBlock*blockSize), (int) numSampleFrames);
        }
    }
    
	//	figure out what sort of blit we need to do
	if((streamFormat->fSampleFormat == kIOAudioStreamSampleFormatLinearPCM) && streamFormat->fIsMixable)
	{
		//	it's linear PCM, which means the target is Float32 and we will be calling a blitter, which works in samples not frames
		Float32* theTargetBuffer = (Float32*)destBuf;
        const UInt32 theFirstSample = firstSampleFrame * inputStream->format.fNumChannels;
        const UInt32 theNumberSamples = numSampleFrames * inputStream->format.fNumChannels;
        
		if(streamFormat->fNumericRepresentation == kIOAudioStreamNumericRepresentationSignedInt)
		{
			//	it's some kind of signed integer, which we handle as some kind of even byte length
			bool nativeEndianInts;
#if TARGET_RT_BIG_ENDIAN
            nativeEndianInts = (streamFormat->fByteOrder == kIOAudioStreamByteOrderBigEndian);
#else
            nativeEndianInts = (streamFormat->fByteOrder == kIOAudioStreamByteOrderLittleEndian);
#endif
			
			switch(streamFormat->fBitWidth)
			{
				case 8:
                {
                    IOLog("REACAudioEngine::convertInputSamples(): can't handle signed "\
                          "integers with a bit width of 8 at the moment.\n");
                }
					break;
                    
				case 16:
                {
                    SInt16* theSourceBuffer = (SInt16*)sampleBuf;
                    if (nativeEndianInts)
                        NativeInt16ToFloat32(&(theSourceBuffer[theFirstSample]), theTargetBuffer, theNumberSamples);
                    else
                        SwapInt16ToFloat32(&(theSourceBuffer[theFirstSample]), theTargetBuffer, theNumberSamples);
                }
					break;
                    
				case 24:
                {
                    UInt8* theSourceBuffer = (UInt8*)sampleBuf;
                    if (nativeEndianInts)
                        NativeInt24ToFloat32(&(theSourceBuffer[3*theFirstSample]), theTargetBuffer, theNumberSamples);
                    else
                        SwapInt24ToFloat32(&(theSourceBuffer[3*theFirstSample]), theTargetBuffer, theNumberSamples);
                }
					break;
                    
				case 32:
                {
                    SInt32* theSourceBuffer = (SInt32*)sampleBuf;
                    if (nativeEndianInts)
                        NativeInt32ToFloat32(&(theSourceBuffer[theFirstSample]), theTargetBuffer, theNumberSamples);
                    else
                        SwapInt32ToFloat32(&(theSourceBuffer[theFirstSample]), theTargetBuffer, theNumberSamples);
                }
					break;
                    
				default:
					IOLog("REACAudioEngine::convertInputSamples(): can't handle signed integers with a bit width of %d.\n",
                          streamFormat->fBitWidth);
					break;
                    
			}
		}
		else if(streamFormat->fNumericRepresentation == kIOAudioStreamNumericRepresentationIEEE754Float)
		{
			//	it is some kind of floating point format
#if TARGET_RT_BIG_ENDIAN
			if((streamFormat->fBitWidth == 32) && (streamFormat->fBitDepth == 32) && (streamFormat->fByteOrder == kIOAudioStreamByteOrderBigEndian))
#else
                if((streamFormat->fBitWidth == 32) && (streamFormat->fBitDepth == 32) && (streamFormat->fByteOrder == kIOAudioStreamByteOrderLittleEndian))
#endif
                {
                    //	it's Float32, so we are just going to copy the data
                    Float32* theSourceBuffer = (Float32*)sampleBuf;
                    memcpy(theTargetBuffer, &(theSourceBuffer[theFirstSample]), theNumberSamples * sizeof(Float32));
                }
                else
                {
                    IOLog("REACEngine::convertInputSamples(): can't handle floats with a bit width of %d, "\
                          "bit depth of %d, and/or the given byte order.\n",
                          streamFormat->fBitWidth, streamFormat->fBitDepth);
                }
		}
	}
	else
	{
		//	it's not linear PCM or it's not mixable, so just copy the data into the target buffer
		SInt8* theSourceBuffer = (SInt8*)sampleBuf;
		UInt32 theFirstByte = firstSampleFrame * (streamFormat->fBitWidth / 8) * streamFormat->fNumChannels;
		UInt32 theNumberBytes = numSampleFrames     * (streamFormat->fBitWidth / 8) * streamFormat->fNumChannels;
		memcpy(destBuf, &(theSourceBuffer[theFirstByte]), theNumberBytes);
	}
    
	return kIOReturnSuccess;
}
