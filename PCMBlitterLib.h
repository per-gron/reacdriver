/*
 *  PCMBlitterLib.h
 *  REAC
 */

/*  Copyright © 2007 Apple Inc. All Rights Reserved.
 *
 *  Disclaimer: IMPORTANT:  This Apple software is supplied to you by 
 *  Apple Inc. ("Apple") in consideration of your agreement to the
 *  following terms, and your use, installation, modification or
 *  redistribution of this Apple software constitutes acceptance of these
 *  terms.  If you do not agree with these terms, please do not use,
 *  install, modify or redistribute this Apple software.
 *
 *  In consideration of your agreement to abide by the following terms, and
 *  subject to these terms, Apple grants you a personal, non-exclusive
 *  license, under Apple's copyrights in this original Apple software (the
 *  "Apple Software"), to use, reproduce, modify and redistribute the Apple
 *  Software, with or without modifications, in source and/or binary forms;
 *  provided that if you redistribute the Apple Software in its entirety and
 *  without modifications, you must retain this notice and the following
 *  text and disclaimers in all such redistributions of the Apple Software. 
 *  Neither the name, trademarks, service marks or logos of Apple Inc. 
 *  may be used to endorse or promote products derived from the Apple
 *  Software without specific prior written permission from Apple.  Except
 *  as expressly stated in this notice, no other rights or licenses, express
 *  or implied, are granted by Apple herein, including but not limited to
 *  any patent rights that may be infringed by your derivative works or by
 *  other works in which the Apple Software may be incorporated.
 *
 *  The Apple Software is provided by Apple on an "AS IS" basis.  APPLE
 *  MAKES NO WARRANTIES, EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION
 *  THE IMPLIED WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE, REGARDING THE APPLE SOFTWARE OR ITS USE AND
 *  OPERATION ALONE OR IN COMBINATION WITH YOUR PRODUCTS.
 *
 *  IN NO EVENT SHALL APPLE BE LIABLE FOR ANY SPECIAL, INDIRECT, INCIDENTAL
 *  OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) ARISING IN ANY WAY OUT OF THE USE, REPRODUCTION,
 *  MODIFICATION AND/OR DISTRIBUTION OF THE APPLE SOFTWARE, HOWEVER CAUSED
 *  AND WHETHER UNDER THEORY OF CONTRACT, TORT (INCLUDING NEGLIGENCE),
 *  STRICT LIABILITY OR OTHERWISE, EVEN IF APPLE HAS BEEN ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _PCMBLITTERLIB_H
#define _PCMBLITTERLIB_H

#ifdef __cplusplus
extern "C" {
#endif

#if !KERNEL
	#include <CoreAudio/CoreAudioTypes.h>
#else
	#include <TargetConditionals.h>
	#include <libkern/OSBase.h>
	#include <libkern/OSTypes.h>
	#include <libkern/OSByteOrder.h>

	typedef float	Float32;
	typedef double	Float64;
#endif


void NativeInt16ToFloat32_X86( const SInt16 *src, Float32 *dst, unsigned int numToConvert );
void SwapInt16ToFloat32_X86( const SInt16 *src, Float32 *dst, unsigned int numToConvert );
void NativeInt24ToFloat32_X86( const UInt8 *src, Float32 *dst, unsigned int numToConvert );
void SwapInt24ToFloat32_X86( const UInt8 *src, Float32 *dst, unsigned int numToConvert );
void NativeInt32ToFloat32_X86( const SInt32 *src, Float32 *dst, unsigned int numToConvert );
void SwapInt32ToFloat32_X86( const SInt32 *src, Float32 *dst, unsigned int numToConvert );

void Float32ToNativeInt16_X86( const Float32 *src, SInt16 *dst, unsigned int numToConvert );
void Float32ToSwapInt16_X86( const Float32 *src, SInt16 *dst, unsigned int numToConvert );
void Float32ToNativeInt32_X86( const Float32 *src, SInt32 *dst, unsigned int numToConvert );
void Float32ToSwapInt32_X86( const Float32 *src, SInt32 *dst, unsigned int numToConvert );
void Float32ToNativeInt24_X86( const Float32 *src, UInt8 *dst, unsigned int numToConvert );
void Float32ToSwapInt24_X86( const Float32 *src, UInt8 *dst, unsigned int numToConvert );

#define NativeInt16ToFloat32 NativeInt16ToFloat32_X86
#define SwapInt16ToFloat32 SwapInt16ToFloat32_X86
#define NativeInt24ToFloat32 NativeInt24ToFloat32_X86
#define SwapInt24ToFloat32 SwapInt24ToFloat32_X86
#define NativeInt32ToFloat32 NativeInt32ToFloat32_X86
#define SwapInt32ToFloat32 SwapInt32ToFloat32_X86

#define Float32ToNativeInt16 Float32ToNativeInt16_X86
#define Float32ToSwapInt16 Float32ToSwapInt16_X86
#define Float32ToNativeInt32 Float32ToNativeInt32_X86
#define Float32ToSwapInt32 Float32ToSwapInt32_X86
#define Float32ToNativeInt24 Float32ToNativeInt24_X86
#define Float32ToSwapInt24 Float32ToSwapInt24_X86

void	Float32ToUInt8(const Float32 *src, UInt8 *dest, unsigned int count);
void	Float32ToSInt8(const Float32 *src, SInt8 *dest, unsigned int count);
void	UInt8ToFloat32(const UInt8 *src, Float32 *dest, unsigned int count);
void	SInt8ToFloat32(const UInt8 *src, Float32 *dest, unsigned int count);

// ____________________________________________________________
// FloatToInt
// N.B. Functions which use this should invoke SET_ROUNDMODE / RESTORE_ROUNDMODE.
static inline SInt32 FloatToInt(double inf, double min32, double max32)
{
	if (inf >= max32) return 0x7FFFFFFF;
	return (SInt32)inf;	// x86 saturates low by itself
}


#ifdef __cplusplus
}
#endif

#endif

