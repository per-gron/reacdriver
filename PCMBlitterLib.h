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

