/*
 *  PCMBlitterLib.cpp
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

// This file has to be compiled with -O3. Compiling with any other optimization
// setting can result in slow execution, improper results and crashes.

#include "FPU.h"
#include "PCMBlitterLib.h"
#include <xmmintrin.h>
#include <libkern/OSByteOrder.h>

#define kMaxFloat32 2147483520.0f
	// this is the biggest floating point number that result from a 32-bit int (bits are lost)
	// it's 2^31 - 128
#define kTwoToMinus31 ((Float32)(1.0/2147483648.0))


static inline __m128i  byteswap16( __m128i v )
{
	//rotate each 16 bit quantity by 8 bits
	return _mm_or_si128( _mm_slli_epi16( v, 8 ), _mm_srli_epi16( v, 8 ) );
}

static inline __m128i  byteswap32( __m128i v )
{
	//rotate each 32 bit quantity by 16 bits
	// 0xB1 = 10110001 = 2,3,0,1
	v = _mm_shufflehi_epi16( _mm_shufflelo_epi16( v, 0xB1 ), 0xB1 );
	return byteswap16( v );
}

void Float32ToNativeInt16_X86( const Float32 *src, SInt16 *dst, unsigned int numToConvert )
{
	const float *src0 = src;
	int16_t *dst0 = dst;
	unsigned int count = numToConvert;
	
	if (count >= 8) {
		int falign = (int)((uintptr_t)src) & 0xF;
		int ialign = (int)((uintptr_t)dst) & 0xF;
	
		if (ialign & 1) goto Scalar;

		// vector -- requires 8+ samples
		ROUNDMODE_NEG_INF
		const __m128 vround = (const __m128) { 0.5f, 0.5f, 0.5f, 0.5f };
		const __m128 vscale = (const __m128) { 32768.0f, 32768.0f, 32768.0f, 32768.0f  };
		__m128 vf0, vf1;
		__m128i vi0, vi1, vpack0;
	
#define F32TOLE16 \
		vf0 = _mm_mul_ps(vf0, vscale);			\
		vf1 = _mm_mul_ps(vf1, vscale);			\
		vf0 = _mm_add_ps(vf0, vround);			\
		vf1 = _mm_add_ps(vf1, vround);			\
		vi0 = _mm_cvtps_epi32(vf0);			\
		vi1 = _mm_cvtps_epi32(vf1);			\
		vpack0 = _mm_packs_epi32(vi0, vi1);
			// mm_packs_epi32 saturates

		if (falign != 0 || ialign != 0) {
			// do one unaligned conversion
			vf0 = _mm_loadu_ps(src);
			vf1 = _mm_loadu_ps(src+4);
			F32TOLE16
			_mm_storeu_si128((__m128i *)dst, vpack0);
			
			// advance such that the destination ints are aligned
			unsigned int n = (16 - ialign) / 2;
			src += n;
			dst += n;
			count -= n;

			falign = (int)((uintptr_t)src) & 0xF;
			if (falign != 0) {
				// unaligned loads, aligned stores
				while (count >= 8) {
					vf0 = _mm_loadu_ps(src);
					vf1 = _mm_loadu_ps(src+4);
					F32TOLE16
					_mm_store_si128((__m128i *)dst, vpack0);
					src += 8;
					dst += 8;
					count -= 8;
				}
				goto VectorCleanup;
			}
		}
	
		// aligned loads, aligned stores
		while (count >= 8) {
			vf0 = _mm_load_ps(src);
			vf1 = _mm_load_ps(src+4);
			F32TOLE16
			_mm_store_si128((__m128i *)dst, vpack0);
			
			src += 8;
			dst += 8;
			count -= 8;
		}
VectorCleanup:
		if (count > 0) {
			// unaligned cleanup -- just do one unaligned vector at the end
			src = src0 + numToConvert - 8;
			dst = dst0 + numToConvert - 8;
			vf0 = _mm_loadu_ps(src);
			vf1 = _mm_loadu_ps(src+4);
			F32TOLE16
			_mm_storeu_si128((__m128i *)dst, vpack0);
		}
		RESTORE_ROUNDMODE
		return;
	}
	
Scalar:
	// scalar for small numbers of samples
	if (count > 0) {
		double scale = 2147483648.0, round = 32768.0, max32 = 2147483648.0 - 1.0 - 32768.0, min32 = 0.;
		SET_ROUNDMODE
		
		while (count-- > 0) {
			double f0 = *src++;
			f0 = f0 * scale + round;
			SInt32 i0 = FloatToInt(f0, min32, max32);
			i0 >>= 16;
			*dst++ = i0;
		}
		RESTORE_ROUNDMODE
	}
}

// ===================================================================================================

void Float32ToSwapInt16_X86( const Float32 *src, SInt16 *dst, unsigned int numToConvert )
{
	const float *src0 = src;
	int16_t *dst0 = dst;
	unsigned int count = numToConvert;
	
	if (count >= 8) {
		// vector -- requires 8+ samples
		unsigned int falign = (unsigned int)((uintptr_t)src) & 0xF;
		unsigned int ialign = (unsigned int)((uintptr_t)dst) & 0xF;
	
		if (falign & 3) goto Scalar;

		ROUNDMODE_NEG_INF
		const __m128 vround = (const __m128) { 0.5f, 0.5f, 0.5f, 0.5f };
		const __m128 vscale = (const __m128) { 32768.0f, 32768.0f, 32768.0f, 32768.0f  };
		__m128 vf0, vf1;
		__m128i vi0, vi1, vpack0;
	
#define F32TOBE16 \
		vf0 = _mm_mul_ps(vf0, vscale);			\
		vf1 = _mm_mul_ps(vf1, vscale);			\
		vf0 = _mm_add_ps(vf0, vround);			\
		vf1 = _mm_add_ps(vf1, vround);			\
		vi0 = _mm_cvtps_epi32(vf0);			\
		vi1 = _mm_cvtps_epi32(vf1);			\
		vpack0 = _mm_packs_epi32(vi0, vi1);		\
		vpack0 = byteswap16(vpack0);
			// mm_packs_epi32 saturates

		if (falign != 0 || ialign != 0) {
			// do one unaligned conversion
			vf0 = _mm_loadu_ps(src);
			vf1 = _mm_loadu_ps(src+4);
			F32TOBE16
			_mm_storeu_si128((__m128i *)dst, vpack0);
			
			// and advance such that the destination ints are aligned
			unsigned int n = (16 - ialign) / 2;
			src += n;
			dst += n;
			count -= n;

			falign = (unsigned int)((uintptr_t)src) & 0xF;
			if (falign != 0) {
				// unaligned loads, aligned stores
				while (count >= 8) {
					vf0 = _mm_loadu_ps(src);
					vf1 = _mm_loadu_ps(src+4);
					F32TOBE16
					_mm_store_si128((__m128i *)dst, vpack0);
					src += 8;
					dst += 8;
					count -= 8;
				}
				goto VectorCleanup;
			}
		}
	
		// aligned loads, aligned stores
		while (count >= 8) {
			vf0 = _mm_load_ps(src);
			vf1 = _mm_load_ps(src+4);
			F32TOBE16
			_mm_store_si128((__m128i *)dst, vpack0);
			
			src += 8;
			dst += 8;
			count -= 8;
		}
VectorCleanup:
		if (count > 0) {
			// unaligned cleanup -- just do one unaligned vector at the end
			src = src0 + numToConvert - 8;
			dst = dst0 + numToConvert - 8;
			vf0 = _mm_loadu_ps(src);
			vf1 = _mm_loadu_ps(src+4);
			F32TOBE16
			_mm_storeu_si128((__m128i *)dst, vpack0);
		}
		RESTORE_ROUNDMODE
		return;
	}
	
	// scalar for small numbers of samples
Scalar:
	if (count > 0) {
		double scale = 2147483648.0, round = 32768.0, max32 = 2147483648.0 - 1.0 - 32768.0, min32 = 0.;
		SET_ROUNDMODE
		
		while (count-- > 0) {
			double f0 = *src++;
			f0 = f0 * scale + round;
			SInt32 i0 = FloatToInt(f0, min32, max32);
			i0 >>= 16;
			OSWriteBigInt16(dst++, 0, i0);
		}
		RESTORE_ROUNDMODE
	}
}

// ===================================================================================================

void Float32ToNativeInt32_X86( const Float32 *src, SInt32 *dst, unsigned int numToConvert )
{
	const float *src0 = src;
	SInt32 *dst0 = dst;
	unsigned int count = numToConvert;
	
	if (count >= 4) {
		int falign = (int)((uintptr_t)src) & 0xF;
		int ialign = (int)((uintptr_t)dst) & 0xF;
		
		if (ialign & 3) goto Scalar;
	
		// vector -- requires 4+ samples
		ROUNDMODE_NEG_INF
		const __m128 vround = (const __m128) { 0.5f, 0.5f, 0.5f, 0.5f };
		const __m128 vmin = (const __m128) { -2147483648.0f, -2147483648.0f, -2147483648.0f, -2147483648.0f };
		const __m128 vmax = (const __m128) { kMaxFloat32, kMaxFloat32, kMaxFloat32, kMaxFloat32  };
		const __m128 vscale = (const __m128) { 2147483648.0f, 2147483648.0f, 2147483648.0f, 2147483648.0f  };
		__m128 vf0;
		__m128i vi0;
	
#define F32TOLE32(x) \
		vf##x = _mm_mul_ps(vf##x, vscale);			\
		vf##x = _mm_add_ps(vf##x, vround);			\
		vf##x = _mm_max_ps(vf##x, vmin);			\
		vf##x = _mm_min_ps(vf##x, vmax);			\
		vi##x = _mm_cvtps_epi32(vf##x);			\

		if (falign != 0 || ialign != 0) {
			// do one unaligned conversion
			vf0 = _mm_loadu_ps(src);
			F32TOLE32(0)
			_mm_storeu_si128((__m128i *)dst, vi0);
			
			// and advance such that the source floats are aligned
			unsigned int n = (16 - falign) / 4;
			src += n;
			dst += n;
			count -= n;

			ialign = (int)((uintptr_t)dst) & 0xF;
			if (ialign != 0) {
				// aligned loads, unaligned stores
				while (count >= 4) {
					vf0 = _mm_load_ps(src);
					F32TOLE32(0)
					_mm_storeu_si128((__m128i *)dst, vi0);
					src += 4;
					dst += 4;
					count -= 4;
				}
				goto VectorCleanup;
			}
		}
	
		while (count >= 4) {
			vf0 = _mm_load_ps(src);
			F32TOLE32(0)
			_mm_store_si128((__m128i *)dst, vi0);
			
			src += 4;
			dst += 4;
			count -= 4;
		}
VectorCleanup:
		if (count > 0) {
			// unaligned cleanup -- just do one unaligned vector at the end
			src = src0 + numToConvert - 4;
			dst = dst0 + numToConvert - 4;
			vf0 = _mm_loadu_ps(src);
			F32TOLE32(0)
			_mm_storeu_si128((__m128i *)dst, vi0);
		}
		RESTORE_ROUNDMODE
		return;
	}
	
	// scalar for small numbers of samples
Scalar:
	if (count > 0) {
		double scale = 2147483648.0, round = 0.5, max32 = 2147483648.0 - 1.0 - 0.5, min32 = 0.;
		SET_ROUNDMODE
		
		while (count-- > 0) {
			double f0 = *src++;
			f0 = f0 * scale + round;
			SInt32 i0 = FloatToInt(f0, min32, max32);
			*dst++ = i0;
		}
		RESTORE_ROUNDMODE
	}
}

// ===================================================================================================

void Float32ToSwapInt32_X86( const Float32 *src, SInt32 *dst, unsigned int numToConvert )
{
	const float *src0 = src;
	SInt32 *dst0 = dst;
	unsigned int count = numToConvert;
	
	if (count >= 4) {
		int falign = (int)((uintptr_t)src) & 0xF;
		int ialign = (int)((uintptr_t)dst) & 0xF;
		
		if (falign & 3) goto Scalar;
	
		// vector -- requires 4+ samples
		ROUNDMODE_NEG_INF
		const __m128 vround = (const __m128) { 0.5f, 0.5f, 0.5f, 0.5f };
		const __m128 vmin = (const __m128) { -2147483648.0f, -2147483648.0f, -2147483648.0f, -2147483648.0f };
		const __m128 vmax = (const __m128) { kMaxFloat32, kMaxFloat32, kMaxFloat32, kMaxFloat32  };
		const __m128 vscale = (const __m128) { 2147483648.0f, 2147483648.0f, 2147483648.0f, 2147483648.0f  };
		__m128 vf0;
		__m128i vi0;
	
#define F32TOBE32(x) \
		vf##x = _mm_mul_ps(vf##x, vscale);			\
		vf##x = _mm_add_ps(vf##x, vround);			\
		vf##x = _mm_max_ps(vf##x, vmin);			\
		vf##x = _mm_min_ps(vf##x, vmax);			\
		vi##x = _mm_cvtps_epi32(vf##x);			\
		vi##x = byteswap32(vi##x);

		if (falign != 0 || ialign != 0) {
			// do one unaligned conversion
			vf0 = _mm_loadu_ps(src);
			F32TOBE32(0)
			_mm_storeu_si128((__m128i *)dst, vi0);
			
			// and advance such that the source floats are aligned
			unsigned int n = (16 - falign) / 4;
			src += n;
			dst += n;
			count -= n;

			ialign = (int)((uintptr_t)dst) & 0xF;
			if (ialign != 0) {
				// aligned loads, unaligned stores
				while (count >= 4) {
					vf0 = _mm_load_ps(src);
					F32TOBE32(0)
					_mm_storeu_si128((__m128i *)dst, vi0);
					src += 4;
					dst += 4;
					count -= 4;
				}
				goto VectorCleanup;
			}
		}
	
		while (count >= 4) {
			vf0 = _mm_load_ps(src);
			F32TOBE32(0)
			_mm_store_si128((__m128i *)dst, vi0);
			
			src += 4;
			dst += 4;
			count -= 4;
		}
VectorCleanup:
		if (count > 0) {
			// unaligned cleanup -- just do one unaligned vector at the end
			src = src0 + numToConvert - 4;
			dst = dst0 + numToConvert - 4;
			vf0 = _mm_loadu_ps(src);
			F32TOBE32(0)
			_mm_storeu_si128((__m128i *)dst, vi0);
		}
		RESTORE_ROUNDMODE
		return;
	}
	
	// scalar for small numbers of samples
Scalar:
	if (count > 0) {
		double scale = 2147483648.0, round = 0.5, max32 = 2147483648.0 - 1.0 - 0.5, min32 = 0.;
		SET_ROUNDMODE
		
		while (count-- > 0) {
			double f0 = *src++;
			f0 = f0 * scale + round;
			SInt32 i0 = FloatToInt(f0, min32, max32);
			OSWriteBigInt32(dst++, 0, i0);
		}
		RESTORE_ROUNDMODE
	}
}

void NativeInt32ToFloat32_X86( const SInt32 *src, Float32 *dst, unsigned int numToConvert )
{
	const SInt32 *src0 = src;
	Float32 *dst0 = dst;
	unsigned int count = numToConvert;

	if (count >= 4) {
		int ialign = (int)((uintptr_t)src) & 0xF;
		int falign = (int)((uintptr_t)dst) & 0xF;
		
		if (falign & 3) goto Scalar;
	
		// vector -- requires 4+ samples
#define LEI32TOF32(x) \
	vf##x = _mm_cvtepi32_ps(vi##x); \
	vf##x = _mm_mul_ps(vf##x, vscale); \
		
		const __m128 vscale = (const __m128) { kTwoToMinus31, kTwoToMinus31, kTwoToMinus31, kTwoToMinus31  };
		__m128 vf0;
		__m128i vi0;

		if (falign != 0 || ialign != 0) {
			// do one unaligned conversion
			vi0 = _mm_loadu_si128((__m128i const *)src);
			LEI32TOF32(0)
			_mm_storeu_ps(dst, vf0);
			
			// and advance such that the destination floats are aligned
			unsigned int n = (16 - falign) / 4;
			src += n;
			dst += n;
			count -= n;

			ialign = (int)((uintptr_t)src) & 0xF;
			if (ialign != 0) {
				// unaligned loads, aligned stores
				while (count >= 4) {
					vi0 = _mm_loadu_si128((__m128i const *)src);
					LEI32TOF32(0)
					_mm_store_ps(dst, vf0);
					src += 4;
					dst += 4;
					count -= 4;
				}
				goto VectorCleanup;
			}
		}
	
		// aligned loads, aligned stores
		while (count >= 4) {
			vi0 = _mm_load_si128((__m128i const *)src);
			LEI32TOF32(0)
			_mm_store_ps(dst, vf0);
			src += 4;
			dst += 4;
			count -= 4;
		}
		
VectorCleanup:
		if (count > 0) {
			// unaligned cleanup -- just do one unaligned vector at the end
			src = src0 + numToConvert - 4;
			dst = dst0 + numToConvert - 4;
			vi0 = _mm_loadu_si128((__m128i const *)src);
			LEI32TOF32(0)
			_mm_storeu_ps(dst, vf0);
		}
		return;
	}
	// scalar for small numbers of samples
Scalar:
	if (count > 0) {
		double scale = 1./2147483648.0f;
		while (count-- > 0) {
			SInt32 i = *src++;
			double f = (double)i * scale;
			*dst++ = (Float32)f;
		}
	}
}

void SwapInt32ToFloat32_X86( const SInt32 *src, Float32 *dst, unsigned int numToConvert )
{
	const SInt32 *src0 = src;
	Float32 *dst0 = dst;
	unsigned int count = numToConvert;

	if (count >= 4) {
		int ialign = (int)((uintptr_t)src) & 0xF;
		int falign = (int)((uintptr_t)dst) & 0xF;
		
		if (falign & 3) goto Scalar;
	
		// vector -- requires 4+ samples
#define BEI32TOF32(x) \
	vi##x = byteswap32(vi##x); \
	vf##x = _mm_cvtepi32_ps(vi##x); \
	vf##x = _mm_mul_ps(vf##x, vscale); \
		
		const __m128 vscale = (const __m128) { kTwoToMinus31, kTwoToMinus31, kTwoToMinus31, kTwoToMinus31  };
		__m128 vf0;
		__m128i vi0;

		if (falign != 0 || ialign != 0) {
			// do one unaligned conversion
			vi0 = _mm_loadu_si128((__m128i const *)src);
			BEI32TOF32(0)
			_mm_storeu_ps(dst, vf0);
			
			// and advance such that the destination floats are aligned
			unsigned int n = (16 - falign) / 4;
			src += n;
			dst += n;
			count -= n;

			ialign = (int)((uintptr_t)src) & 0xF;
			if (ialign != 0) {
				// unaligned loads, aligned stores
				while (count >= 4) {
					vi0 = _mm_loadu_si128((__m128i const *)src);
					BEI32TOF32(0)
					_mm_store_ps(dst, vf0);
					src += 4;
					dst += 4;
					count -= 4;
				}
				goto VectorCleanup;
			}
		}
	
		// aligned loads, aligned stores
		while (count >= 4) {
			vi0 = _mm_load_si128((__m128i const *)src);
			BEI32TOF32(0)
			_mm_store_ps(dst, vf0);
			src += 4;
			dst += 4;
			count -= 4;
		}
		
VectorCleanup:
		if (count > 0) {
			// unaligned cleanup -- just do one unaligned vector at the end
			src = src0 + numToConvert - 4;
			dst = dst0 + numToConvert - 4;
			vi0 = _mm_loadu_si128((__m128i const *)src);
			BEI32TOF32(0)
			_mm_storeu_ps(dst, vf0);
		}
		return;
	}
	// scalar for small numbers of samples
Scalar:
	if (count > 0) {
		double scale = 1./2147483648.0f;
		while (count-- > 0) {
			SInt32 i = OSReadBigInt32(src++, 0);
			double f = (double)i * scale;
			*dst++ = (Float32)f;
		}
	}
}

void NativeInt16ToFloat32_X86( const SInt16 *src, Float32 *dst, unsigned int numToConvert )
{
	const SInt16 *src0 = src;
	Float32 *dst0 = dst;
	unsigned int count = numToConvert;

	if (count >= 8) {
		int ialign = (int)((uintptr_t)src) & 0xF;
		int falign = (int)((uintptr_t)dst) & 0xF;
	
		if (falign & 3) goto Scalar;

		// vector -- requires 8+ samples
		// convert the 16-bit words to the high word of 32-bit values
#define LEI16TOF32(x, y) \
	vi##x = _mm_unpacklo_epi16(zero, vpack##x); \
	vi##y = _mm_unpackhi_epi16(zero, vpack##x); \
	vf##x = _mm_cvtepi32_ps(vi##x); \
	vf##y = _mm_cvtepi32_ps(vi##y); \
	vf##x = _mm_mul_ps(vf##x, vscale); \
	vf##y = _mm_mul_ps(vf##y, vscale);
		
		const __m128 vscale = (const __m128) { kTwoToMinus31, kTwoToMinus31, kTwoToMinus31, kTwoToMinus31  };
		const __m128i zero = _mm_setzero_si128();
		__m128 vf0, vf1;
		__m128i vi0, vi1, vpack0;

		if (falign != 0 || ialign != 0) {
			// do one unaligned conversion
			vpack0 = _mm_loadu_si128((__m128i const *)src);
			LEI16TOF32(0, 1)
			_mm_storeu_ps(dst, vf0);
			_mm_storeu_ps(dst+4, vf1);
			
			// and advance such that the destination floats are aligned
			unsigned int n = (16 - falign) / 4;
			src += n;
			dst += n;
			count -= n;

			ialign = (int)((uintptr_t)src) & 0xF;
			if (ialign != 0) {
				// unaligned loads, aligned stores
				while (count >= 8) {
					vpack0 = _mm_loadu_si128((__m128i const *)src);
					LEI16TOF32(0, 1)
					_mm_store_ps(dst, vf0);
					_mm_store_ps(dst+4, vf1);
					src += 8;
					dst += 8;
					count -= 8;
				}
				goto VectorCleanup;
			}
		}
	
		// aligned loads, aligned stores
		while (count >= 8) {
			vpack0 = _mm_load_si128((__m128i const *)src);
			LEI16TOF32(0, 1)
			_mm_store_ps(dst, vf0);
			_mm_store_ps(dst+4, vf1);
			src += 8;
			dst += 8;
			count -= 8;
		}
		
VectorCleanup:
		if (count > 0) {
			// unaligned cleanup -- just do one unaligned vector at the end
			src = src0 + numToConvert - 8;
			dst = dst0 + numToConvert - 8;
			vpack0 = _mm_loadu_si128((__m128i const *)src);
			LEI16TOF32(0, 1)
			_mm_storeu_ps(dst, vf0);
			_mm_storeu_ps(dst+4, vf1);
		}
		return;
	}
	// scalar for small numbers of samples
Scalar:
	if (count > 0) {
		double scale = 1./32768.f;
		while (count-- > 0) {
			SInt16 i = *src++;
			double f = (double)i * scale;
			*dst++ = (Float32)f;
		}
	}
}

// ===================================================================================================

void SwapInt16ToFloat32_X86( const SInt16 *src, Float32 *dst, unsigned int numToConvert )
{
	const SInt16 *src0 = src;
	Float32 *dst0 = dst;
	unsigned int count = numToConvert;

	if (count >= 8) {
		int ialign = (int)((uintptr_t)src) & 0xF;
		int falign = (int)((uintptr_t)dst) & 0xF;
		
		if (falign & 3) goto Scalar;
	
		// vector -- requires 8+ samples
		// convert the 16-bit words to the high word of 32-bit values
#define BEI16TOF32 \
	vpack0 = byteswap16(vpack0); \
	vi0 = _mm_unpacklo_epi16(zero, vpack0); \
	vi1 = _mm_unpackhi_epi16(zero, vpack0); \
	vf0 = _mm_cvtepi32_ps(vi0); \
	vf1 = _mm_cvtepi32_ps(vi1); \
	vf0 = _mm_mul_ps(vf0, vscale); \
	vf1 = _mm_mul_ps(vf1, vscale);
		
		const __m128 vscale = (const __m128) { kTwoToMinus31, kTwoToMinus31, kTwoToMinus31, kTwoToMinus31  };
		const __m128i zero = _mm_setzero_si128();
		__m128 vf0, vf1;
		__m128i vi0, vi1, vpack0;

		if (falign != 0 || ialign != 0) {
			// do one unaligned conversion
			vpack0 = _mm_loadu_si128((__m128i const *)src);
			BEI16TOF32
			_mm_storeu_ps(dst, vf0);
			_mm_storeu_ps(dst+4, vf1);

			// and advance such that the destination floats are aligned
			unsigned int n = (16 - falign) / 4;
			src += n;
			dst += n;
			count -= n;

			ialign = (int)((uintptr_t)src) & 0xF;
			if (ialign != 0) {
				// unaligned loads, aligned stores
				while (count >= 8) {
					vpack0 = _mm_loadu_si128((__m128i const *)src);
					BEI16TOF32
					_mm_store_ps(dst, vf0);
					_mm_store_ps(dst+4, vf1);
					src += 8;
					dst += 8;
					count -= 8;
				}
				goto VectorCleanup;
			}
		}
	
		// aligned loads, aligned stores
		while (count >= 8) {
			vpack0 = _mm_load_si128((__m128i const *)src);
			BEI16TOF32
			_mm_store_ps(dst, vf0);
			_mm_store_ps(dst+4, vf1);
			src += 8;
			dst += 8;
			count -= 8;
		}
		
VectorCleanup:
		if (count > 0) {
			// unaligned cleanup -- just do one unaligned vector at the end
			src = src0 + numToConvert - 8;
			dst = dst0 + numToConvert - 8;
			vpack0 = _mm_loadu_si128((__m128i const *)src);
			BEI16TOF32
			_mm_storeu_ps(dst, vf0);
			_mm_storeu_ps(dst+4, vf1);
		}
		return;
	}
	// scalar for small numbers of samples
Scalar:
	if (count > 0) {
		double scale = 1./32768.f;
		while (count-- > 0) {
			SInt16 i = OSReadBigInt16(src++, 0);
			double f = (double)i * scale;
			*dst++ = (Float32)f;
		}
	}
}

// ===================================================================================================

#pragma mark -

// load 4 24-bit packed little-endian ints into the high 24 bits of 4 32-bit ints
static inline __m128i UnpackLE24To32(const UInt8 *loadAddr, __m128i mask)
{
	__m128i load = _mm_loadu_si128((__m128i *)loadAddr);
	__m128i result;
	
	load = _mm_slli_si128(load, 1);
	result = _mm_and_si128(load, mask);

	mask = _mm_slli_si128(mask, 3);
	result = _mm_or_si128(result, _mm_slli_si128(_mm_and_si128(load, mask), 1));
	
	mask = _mm_slli_si128(mask, 3);
	result = _mm_or_si128(result, _mm_slli_si128(_mm_and_si128(load, mask), 2));
	
	mask = _mm_slli_si128(mask, 3);
	result = _mm_or_si128(result, _mm_slli_si128(_mm_and_si128(load, mask), 3));
	return result;
}

void NativeInt24ToFloat32_X86( const UInt8 *src, Float32 *dst, unsigned int numToConvert )
{
	const UInt8 *src0 = src;
	Float32 *dst0 = dst;
	unsigned int count = numToConvert;

	if (count >= 6) {
		// vector -- requires 6+ samples (18 source bytes)	
		const __m128 vscale = (const __m128) { kTwoToMinus31, kTwoToMinus31, kTwoToMinus31, kTwoToMinus31  };
		const __m128i mask = _mm_setr_epi32(0xFFFFFF00, 0, 0, 0);
		__m128 vf0;
		__m128i vi0;

		int falign = (int)((uintptr_t)dst) & 0xF;
		
		union {
			UInt32 i[4];
			__m128i v;
		} u;
	
		if (falign != 0) {
			// do one unaligned conversion
			vi0 = UnpackLE24To32(src, mask);
			LEI32TOF32(0)
			_mm_storeu_ps(dst, vf0);
			
			// and advance such that the destination floats are aligned
			unsigned int n = (16 - falign) / 4;
			src += 3*n;
			dst += n;
			count -= n;
		}
	
		// unaligned loads, aligned stores
		while (count >= 6) {
			vi0 = UnpackLE24To32(src, mask);
			LEI32TOF32(0)
			_mm_store_ps(dst, vf0);
			src += 3*4;
			dst += 4;
			count -= 4;
		}

		while (count >= 4) {
			u.i[0] = ((UInt32 *)src)[0];
			u.i[1] = ((UInt32 *)src)[1];
			u.i[2] = ((UInt32 *)src)[2];
			vi0 = UnpackLE24To32((UInt8 *)u.i, mask);
			LEI32TOF32(0)
			_mm_store_ps(dst, vf0);
			src += 3*4;
			dst += 4;
			count -= 4;
		}
		
		if (count > 0) {
			// unaligned cleanup -- just do one unaligned vector at the end
			src = src0 + 3*numToConvert - 12;
			dst = dst0 + numToConvert - 4;
			u.i[0] = ((UInt32 *)src)[0];
			u.i[1] = ((UInt32 *)src)[1];
			u.i[2] = ((UInt32 *)src)[2];
			vi0 = UnpackLE24To32((UInt8 *)u.i, mask);
			LEI32TOF32(0)
			_mm_storeu_ps(dst, vf0);
		}
		return;
	}
	// scalar for small numbers of samples
	if (count > 0) {
		double scale = 1./8388608.0f;
		while (count-- > 0) {
			SInt32 i = ((signed char)src[2] << 16) | (src[1] << 8) | src[0];
			double f = (double)i * scale;
			*dst++ = (Float32)f;
			src += 3;
		}
	}
}

// ===================================================================================================

// load 4 24-bit packed big-endian ints into the high 24 bits of 4 32-bit ints
static inline __m128i UnpackBE24To32(const UInt8 *loadAddr, __m128i mask)
{
	__m128i load = _mm_loadu_si128((__m128i *)loadAddr);
	__m128i result;
	
	result = _mm_and_si128(load, mask);

	mask = _mm_slli_si128(mask, 3);
	result = _mm_or_si128(result, _mm_slli_si128(_mm_and_si128(load, mask), 1));
	
	mask = _mm_slli_si128(mask, 3);
	result = _mm_or_si128(result, _mm_slli_si128(_mm_and_si128(load, mask), 2));
	
	mask = _mm_slli_si128(mask, 3);
	result = _mm_or_si128(result, _mm_slli_si128(_mm_and_si128(load, mask), 3));
	result = byteswap32(result);
	return result;
}

void SwapInt24ToFloat32_X86( const UInt8 *src, Float32 *dst, unsigned int numToConvert )
{
	const UInt8 *src0 = src;
	Float32 *dst0 = dst;
	unsigned int count = numToConvert;

	if (count >= 6) {
		// vector -- requires 6+ samples (18 bytes)
		const __m128 vscale = (const __m128) { kTwoToMinus31, kTwoToMinus31, kTwoToMinus31, kTwoToMinus31  };
		const __m128i mask = _mm_setr_epi32(0xFFFFFF, 0, 0, 0);
		__m128 vf0;
		__m128i vi0;

		int falign = (int)((uintptr_t)dst) & 0xF;
		
		union {
			UInt32 i[4];
			__m128i v;
		} u;
	
		if (falign != 0) {
			// do one unaligned conversion
			vi0 = UnpackBE24To32(src, mask);
			LEI32TOF32(0)
			_mm_storeu_ps(dst, vf0);
			
			// and advance such that the destination floats are aligned
			unsigned int n = (16 - falign) / 4;
			src += 3*n;
			dst += n;
			count -= n;
		}
	
		// unaligned loads, aligned stores
		while (count >= 6) {
			vi0 = UnpackBE24To32(src, mask);
			LEI32TOF32(0)
			_mm_store_ps(dst, vf0);
			src += 3*4;
			dst += 4;
			count -= 4;
		}

		while (count >= 4) {
			u.i[0] = ((UInt32 *)src)[0];
			u.i[1] = ((UInt32 *)src)[1];
			u.i[2] = ((UInt32 *)src)[2];
			vi0 = UnpackBE24To32((UInt8 *)u.i, mask);
			LEI32TOF32(0)
			_mm_store_ps(dst, vf0);
			src += 3*4;
			dst += 4;
			count -= 4;
		}
		
		if (count > 0) {
			// unaligned cleanup -- just do one unaligned vector at the end
			src = src0 + 3*numToConvert - 12;
			dst = dst0 + numToConvert - 4;
			u.i[0] = ((UInt32 *)src)[0];
			u.i[1] = ((UInt32 *)src)[1];
			u.i[2] = ((UInt32 *)src)[2];
			vi0 = UnpackBE24To32((UInt8 *)u.i, mask);
			LEI32TOF32(0)
			_mm_storeu_ps(dst, vf0);
		}
		return;
	}
	// scalar for small numbers of samples
	if (count > 0) {
		double scale = 1./8388608.0f;
		while (count-- > 0) {
			SInt32 i = ((signed char)src[0] << 16) | (src[1] << 8) | src[2];
			double f = (double)i * scale;
			*dst++ = (Float32)f;
			src += 3;
		}
	}
}


// ===================================================================================================

static inline __m128i Pack32ToBE24(__m128i val)
{
	val = byteswap32(val);
	// same as for little-endian except we don't want the initial shift to get rid of the low 8 bits
	__m128i mask = _mm_setr_epi32(0x00FFFFFF, 0, 0, 0);
	
	__m128i store = _mm_and_si128(val, mask);

	val = _mm_srli_si128(val, 1);
	mask = _mm_slli_si128(mask, 3);
	store = _mm_or_si128(store, _mm_and_si128(val, mask));

	val = _mm_srli_si128(val, 1);
	mask = _mm_slli_si128(mask, 3);
	store = _mm_or_si128(store, _mm_and_si128(val, mask));

	val = _mm_srli_si128(val, 1);
	mask = _mm_slli_si128(mask, 3);
	store = _mm_or_si128(store, _mm_and_si128(val, mask));
	return store;
}

// ~14 instructions
static inline __m128i Pack32ToLE24(__m128i val, __m128i mask)
{
	__m128i store;
#if 1
	val = _mm_srli_si128(val, 1);
	store = _mm_and_si128(val, mask);

	val = _mm_srli_si128(val, 1);
	mask = _mm_slli_si128(mask, 3);
	store = _mm_or_si128(store, _mm_and_si128(val, mask));

	val = _mm_srli_si128(val, 1);
	mask = _mm_slli_si128(mask, 3);
	store = _mm_or_si128(store, _mm_and_si128(val, mask));

	val = _mm_srli_si128(val, 1);
	mask = _mm_slli_si128(mask, 3);
	store = _mm_or_si128(store, _mm_and_si128(val, mask));
	return store;

#else
	store = _mm_and_si128(val, mask);
	val = _mm_slli_si128(val, 1);
	mask = _mm_srli_si128(mask, 3);
	store = _mm_or_si128(store, _mm_and_si128(val, mask));
	val = _mm_slli_si128(val, 1);
	mask = _mm_srli_si128(mask, 3);
	store = _mm_or_si128(store, _mm_and_si128(val, mask));
	val = _mm_slli_si128(val, 1);
	mask = _mm_srli_si128(mask, 3);
	store = _mm_or_si128(store, _mm_and_si128(val, mask));
	return _mm_srli_si128(store, 4);	// shift result into most significant 12 bytes
#endif
}

void Float32ToNativeInt24_X86( const Float32 *src, UInt8 *dst, unsigned int numToConvert )
{
	const Float32 *src0 = src;
	UInt8 *dst0 = dst;
	unsigned int count = numToConvert;
	
	if (count >= 6) {
		int falign = (int)((uintptr_t)src) & 0xF;
		if (falign & 3) goto Scalar;
	
		// vector -- requires 6+ samples
		ROUNDMODE_NEG_INF
		const __m128 vround = (const __m128) { 0.5f, 0.5f, 0.5f, 0.5f };
		const __m128 vmin = (const __m128) { -2147483648.0f, -2147483648.0f, -2147483648.0f, -2147483648.0f };
		const __m128 vmax = (const __m128) { kMaxFloat32, kMaxFloat32, kMaxFloat32, kMaxFloat32  };
		const __m128 vscale = (const __m128) { 2147483648.0f, 2147483648.0f, 2147483648.0f, 2147483648.0f  };
		__m128i mask = _mm_setr_epi32(0x00FFFFFF, 0, 0, 0);
			// it is actually cheaper to copy and shift this mask on the fly than to have 4 of them

		__m128i store;
		union {
			UInt32 i[4];
			__m128i v;
		} u;

		__m128 vf0;
		__m128i vi0;

		if (falign != 0) {
			// do one unaligned conversion
			vf0 = _mm_loadu_ps(src);
			F32TOLE32(0)
			store = Pack32ToLE24(vi0, mask);
			_mm_storeu_si128((__m128i *)dst, store);

			// and advance such that the source floats are aligned
			unsigned int n = (16 - falign) / 4;
			src += n;
			dst += 3*n;	// bytes
			count -= n;
		}
	
		while (count >= 6) {
			vf0 = _mm_load_ps(src);
			F32TOLE32(0)
			store = Pack32ToLE24(vi0, mask);
			_mm_storeu_si128((__m128i *)dst, store);	// destination always unaligned
			
			src += 4;
			dst += 12;	// bytes
			count -= 4;
		}
		
		
		if (count >= 4) {
			vf0 = _mm_load_ps(src);
			F32TOLE32(0)
			u.v = Pack32ToLE24(vi0, mask);
			((UInt32 *)dst)[0] = u.i[0];
			((UInt32 *)dst)[1] = u.i[1];
			((UInt32 *)dst)[2] = u.i[2];
			
			src += 4;
			dst += 12;	// bytes
			count -= 4;
		}

		if (count > 0) {
			// unaligned cleanup -- just do one unaligned vector at the end
			src = src0 + numToConvert - 4;
			dst = dst0 + 3*numToConvert - 12;
			vf0 = _mm_loadu_ps(src);
			F32TOLE32(0)
			u.v = Pack32ToLE24(vi0, mask);
			((UInt32 *)dst)[0] = u.i[0];
			((UInt32 *)dst)[1] = u.i[1];
			((UInt32 *)dst)[2] = u.i[2];
		}
		RESTORE_ROUNDMODE
		return;
	}
	
	// scalar for small numbers of samples
Scalar:
	if (count > 0) {
		double scale = 2147483648.0, round = 0.5, max32 = 2147483648.0 - 1.0 - 0.5, min32 = 0.;
		SET_ROUNDMODE
		
		while (count-- > 0) {
			double f0 = *src++;
			f0 = f0 * scale + round;
			UInt32 i0 = FloatToInt(f0, min32, max32);
			dst[0] = (UInt8)(i0 >> 8);
			dst[1] = (UInt8)(i0 >> 16);
			dst[2] = (UInt8)(i0 >> 24);
			dst += 3;
		}
		RESTORE_ROUNDMODE
	}
}

void Float32ToSwapInt24_X86( const Float32 *src, UInt8 *dst, unsigned int numToConvert )
{
	const Float32 *src0 = src;
	UInt8 *dst0 = dst;
	unsigned int count = numToConvert;
	
	if (count >= 6) {
		// vector -- requires 8+ samples
		ROUNDMODE_NEG_INF
		const __m128 vround = (const __m128) { 0.5f, 0.5f, 0.5f, 0.5f };
		const __m128 vmin = (const __m128) { -2147483648.0f, -2147483648.0f, -2147483648.0f, -2147483648.0f };
		const __m128 vmax = (const __m128) { kMaxFloat32, kMaxFloat32, kMaxFloat32, kMaxFloat32  };
		const __m128 vscale = (const __m128) { 2147483648.0f, 2147483648.0f, 2147483648.0f, 2147483648.0f  };

		__m128i store;
		union {
			UInt32 i[4];
			__m128i v;
		} u;

		__m128 vf0;
		__m128i vi0;

		int falign = (int)((uintptr_t)src) & 0xF;
	
		if (falign != 0) {
			// do one unaligned conversion
			vf0 = _mm_loadu_ps(src);
			F32TOLE32(0)
			store = Pack32ToBE24(vi0);
			_mm_storeu_si128((__m128i *)dst, store);

			// and advance such that the source floats are aligned
			unsigned int n = (16 - falign) / 4;
			src += n;
			dst += 3*n;	// bytes
			count -= n;
		}
	
		while (count >= 6) {
			vf0 = _mm_load_ps(src);
			F32TOLE32(0)
			store = Pack32ToBE24(vi0);
			_mm_storeu_si128((__m128i *)dst, store);	// destination always unaligned
			
			src += 4;
			dst += 12;	// bytes
			count -= 4;
		}
		
		
		if (count >= 4) {
			vf0 = _mm_load_ps(src);
			F32TOLE32(0)
			u.v = Pack32ToBE24(vi0);
			((UInt32 *)dst)[0] = u.i[0];
			((UInt32 *)dst)[1] = u.i[1];
			((UInt32 *)dst)[2] = u.i[2];
			
			src += 4;
			dst += 12;	// bytes
			count -= 4;
		}

		if (count > 0) {
			// unaligned cleanup -- just do one unaligned vector at the end
			src = src0 + numToConvert - 4;
			dst = dst0 + 3*numToConvert - 12;
			vf0 = _mm_loadu_ps(src);
			F32TOLE32(0)
			u.v = Pack32ToBE24(vi0);
			((UInt32 *)dst)[0] = u.i[0];
			((UInt32 *)dst)[1] = u.i[1];
			((UInt32 *)dst)[2] = u.i[2];
		}
		RESTORE_ROUNDMODE
		return;
	}
	
	// scalar for small numbers of samples
	if (count > 0) {
		double scale = 2147483648.0, round = 0.5, max32 = 2147483648.0 - 1.0 - 0.5, min32 = 0.;
		SET_ROUNDMODE
		
		while (count-- > 0) {
			double f0 = *src++;
			f0 = f0 * scale + round;
			UInt32 i0 = FloatToInt(f0, min32, max32);
			dst[0] = (UInt8)(i0 >> 24);
			dst[1] = (UInt8)(i0 >> 16);
			dst[2] = (UInt8)(i0 >> 8);
			dst += 3;
		}
		RESTORE_ROUNDMODE
	}
}

// ____________________________________________________________________________
#pragma mark -

class FloatToIntBlitter {
public:
	FloatToIntBlitter(int bitDepth)
	{
		int rightShift = 32 - bitDepth;
		mShift = rightShift;
		mRound = (rightShift > 0) ? double(1L << (rightShift - 1)) : 0.;
	}

protected:
	double	mRound;
	int		mShift;	// how far to shift a 32 bit value right
};

template <class FloatType, class IntType>
class TFloatToIntBlitter : public FloatToIntBlitter {
public:
	typedef typename FloatType::value_type float_val;
	typedef typename IntType::value_type int_val;

	TFloatToIntBlitter(int bitDepth) : FloatToIntBlitter(bitDepth) { }

	void	Convert(const void *vsrc, void *vdest, unsigned int nSamples)
	{
		const float_val *src = (const float_val *)vsrc;
		int_val *dest = (int_val *)vdest;
		double maxInt32 = 2147483648.0;	// 1 << 31
		double round = mRound;
		double max32 = maxInt32 - 1.0 - round;
		double min32 = -2147483648.0;
		int shift = mShift, count;
		double f1, f2, f3, f4;
		int i1, i2, i3, i4;

		SET_ROUNDMODE
		
		if (nSamples >= 8) {
			f1 = FloatType::load(src + 0);
			
			f2 = FloatType::load(src + 1);
			f1 = f1 * maxInt32 + round;
			
			f3 = FloatType::load(src + 2);
			f2 = f2 * maxInt32 + round;
			i1 = FloatToInt(f1, min32, max32);
			
			src += 3;
			
			nSamples -= 4;
			count = nSamples >> 2;
			nSamples &= 3;
			
			while (count--) {
				f4 = FloatType::load(src + 0);
				f3 = f3 * maxInt32 + round;
				i2 = FloatToInt(f2, min32, max32);
				IntType::store(dest + 0, i1 >> shift);
	
				f1 = FloatType::load(src + 1);
				f4 = f4 * maxInt32 + round;
				i3 = FloatToInt(f3, min32, max32);
				IntType::store(dest + 1, i2 >> shift);
	
				f2 = FloatType::load(src + 2);
				f1 = f1 * maxInt32 + round;
				i4 = FloatToInt(f4, min32, max32);
				IntType::store(dest + 2, i3 >> shift);
				
				f3 = FloatType::load(src + 3);
				f2 = f2 * maxInt32 + round;
				i1 = FloatToInt(f1, min32, max32);
				IntType::store(dest + 3, i4 >> shift);
				
				src += 4;
				dest += 4;
			}
			
			f4 = FloatType::load(src);
			f3 = f3 * maxInt32 + round;
			i2 = FloatToInt(f2, min32, max32);
			IntType::store(dest + 0, i1 >> shift);
		
			f4 = f4 * maxInt32 + round;
			i3 = FloatToInt(f3, min32, max32);
			IntType::store(dest + 1, i2 >> shift);

			i4 = FloatToInt(f4, min32, max32);
			IntType::store(dest + 2, i3 >> shift);

			IntType::store(dest + 3, i4 >> shift);
			
			src += 1;
			dest += 4;
		}

		count = nSamples;
		while (count--) {
			f1 = FloatType::load(src) * maxInt32 + round;
			i1 = FloatToInt(f1, min32, max32) >> shift;
			IntType::store(dest, i1);
			src += 1;
			dest += 1;
		}
		RESTORE_ROUNDMODE
	}

};

// IntToFloatBlitter
class IntToFloatBlitter {
public:
	IntToFloatBlitter(int bitDepth) :
		mBitDepth(bitDepth)
	{
		mScale = static_cast<Float32>(1.0 / float(1UL << (bitDepth - 1)));
	}
	
	Float32		mScale;
	UInt32		mBitDepth;
};

template <class IntType, class FloatType>
class TIntToFloatBlitter : public IntToFloatBlitter {
public:
	typedef typename FloatType::value_type float_val;
	typedef typename IntType::value_type int_val;

	TIntToFloatBlitter(int bitDepth) : IntToFloatBlitter(bitDepth) { }

	void	Convert(const void *vsrc, void *vdest, unsigned int nSamples)
	{
		const int_val *src = (const int_val *)vsrc;
		float_val *dest = (float_val *)vdest;
		int count = nSamples;
		Float32 scale = mScale;
		int_val i0, i1, i2, i3;
		float_val f0, f1, f2, f3;
		
		/*
			$i = IntType::load(src); ++src;
			$f = $i;
			$f *= scale;
			FloatType::store(dest, $f); ++dest;
		*/

		if (count >= 4) {
			// Cycle 1
			i0 = IntType::load(src); ++src;

			// Cycle 2
			i1 = IntType::load(src); ++src;
			f0 = i0;

			// Cycle 3
			i2 = IntType::load(src); ++src;
			f1 = i1;
			f0 *= scale;

			// Cycle 4
			i3 = IntType::load(src); ++src;
			f2 = i2;
			f1 *= scale;
			FloatType::store(dest, f0); ++dest;

			count -= 4;
			int loopCount = count / 4;
			count -= 4 * loopCount;

			while (loopCount--) {
				// Cycle A
				i0 = IntType::load(src); ++src;
				f3 = i3;
				f2 *= scale;
				FloatType::store(dest, f1); ++dest;

				// Cycle B
				i1 = IntType::load(src); ++src;
				f0 = i0;
				f3 *= scale;
				FloatType::store(dest, f2); ++dest;

				// Cycle C
				i2 = IntType::load(src); ++src;
				f1 = i1;
				f0 *= scale;
				FloatType::store(dest, f3); ++dest;

				// Cycle D
				i3 = IntType::load(src); ++src;
				f2 = i2;
				f1 *= scale;
				FloatType::store(dest, f0); ++dest;
			}

			// Cycle 3
			f3 = i3;
			f2 *= scale;
			FloatType::store(dest, f1); ++dest;

			// Cycle 2
			f3 *= scale;
			FloatType::store(dest, f2); ++dest;

			// Cycle 1
			FloatType::store(dest, f3); ++dest;
		}

		while (count--) {
			i0 = IntType::load(src); ++src;
			f0 = i0;
			f0 *= scale;
			FloatType::store(dest, f0); ++dest;
		}
	}
};

class PCMFloat32 {
public:
	typedef Float32 value_type;
	
	static value_type load(const value_type *p) { return *p; }
	static void store(value_type *p, float val)	{ *p = val; }
};

class PCMSInt8 {
public:
	typedef SInt8 value_type;
	
	static value_type load(const value_type *p) { return *p; }
	static void store(value_type *p, int val)	{ *p = val; }
};

class PCMUInt8 {
public:
	typedef SInt8 value_type;	// signed so that sign-extending works right
	
	static value_type load(const value_type *p) { return *p ^ 0x80; }
	static void store(value_type *p, int val)	{ *p = val ^ 0x80; }
};

// ____________________________________________________________________________
#pragma mark -

void	Float32ToUInt8(const Float32 *src, UInt8 *dest, unsigned int count)
{
	TFloatToIntBlitter<PCMFloat32, PCMUInt8> blitter(8);
	blitter.Convert(src, dest, count);
}

void	Float32ToSInt8(const Float32 *src, SInt8 *dest, unsigned int count)
{
	TFloatToIntBlitter<PCMFloat32, PCMSInt8> blitter(8);
	blitter.Convert(src, dest, count);
}

void	UInt8ToFloat32(const UInt8 *src, Float32 *dest, unsigned int count)
{
	TIntToFloatBlitter<PCMUInt8, PCMFloat32> blitter(8);
	blitter.Convert(src, dest, count);
}

void	SInt8ToFloat32(const UInt8 *src, Float32 *dest, unsigned int count)
{
	TIntToFloatBlitter<PCMSInt8, PCMFloat32> blitter(8);
	blitter.Convert(src, dest, count);
}

