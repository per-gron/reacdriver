#include "PCMBlitterLib.h"

#ifdef __cplusplus
extern "C"
#endif
void	PCMBlitterLibTest()
{
	unsigned nframes = 0;

	{
		UInt8 *src = 0;
		Float32 *dest = 0;

		UInt8ToFloat32(src, dest, nframes);
		SInt8ToFloat32(src, dest, nframes);
		NativeInt16ToFloat32((SInt16 *)src, dest, nframes);
		SwapInt16ToFloat32((SInt16 *)src, dest, nframes);
		NativeInt16ToFloat32((SInt16 *)src, dest, nframes);
		SwapInt16ToFloat32((SInt16 *)src, dest, nframes);
		NativeInt24ToFloat32(src, dest, nframes);
		SwapInt24ToFloat32(src, dest, nframes);
		NativeInt32ToFloat32((SInt32 *)src, dest, nframes);
		SwapInt32ToFloat32((SInt32 *)src, dest, nframes);
	}
	{
		Float32 *src = 0;
		UInt8 *dest = 0;
		
		Float32ToUInt8(src, dest, nframes);
		Float32ToSInt8(src, (SInt8 *)dest, nframes);
		Float32ToNativeInt16(src, (SInt16 *)dest, nframes);
		Float32ToSwapInt16(src, (SInt16 *)dest, nframes);
		Float32ToNativeInt24(src, dest, nframes);
		Float32ToSwapInt24(src, dest, nframes);
		Float32ToNativeInt32(src, (SInt32 *)dest, nframes);
		Float32ToSwapInt32(src, (SInt32 *)dest, nframes);
	}
}
