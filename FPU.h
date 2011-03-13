#ifndef _FPU_H
#define _FPU_H

#include <TargetConditionals.h>

#if TARGET_OS_MAC && (TARGET_CPU_PPC || TARGET_CPU_PPC64)
	#define SET_ROUNDMODE \
		double oldSetting; \
		/* Set the FPSCR to round to -Inf mode */ \
		{ \
			union { \
				double	d; \
				int		i[2]; \
			} setting; \
			register double newSetting; \
			/* Read the the current FPSCR value */ \
			__asm__ __volatile__ ( "mffs %0" : "=f" ( oldSetting ) ); \
			/* Store it to the stack */ \
			setting.d = oldSetting; \
			/* Read in the low 32 bits and mask off the last two bits so they are zero      */ \
			/* in the integer unit. These two bits set to zero means round to nearest mode. */ \
			/* Finally, then store the result back */ \
			setting.i[1] |= 3; \
			/* Load the new FPSCR setting into the FP register file again */ \
			newSetting = setting.d; \
			/* Change the volatile to the new setting */ \
			__asm__ __volatile__( "mtfsf 7, %0" : : "f" (newSetting ) ); \
		}

	#define RESTORE_ROUNDMODE \
		/* restore the old FPSCR setting */ \
		__asm__ __volatile__ ( "mtfsf 7, %0" : : "f" (oldSetting) );
	#define DISABLE_DENORMALS
	#define RESTORE_DENORMALS
	
#elif TARGET_OS_MAC && (TARGET_CPU_X86 || TARGET_CPU_X86_64)
	// our compiler does ALL floating point with SSE
	#define GETCSR()    ({ int _result; asm volatile ("stmxcsr %0" : "=m" (*&_result) ); /*return*/ _result; })
	#define SETCSR( a )    { int _temp = a; asm volatile( "ldmxcsr %0" : : "m" (*&_temp ) ); }

	#define DISABLE_DENORMALS int _savemxcsr = GETCSR(); SETCSR(_savemxcsr | 0x8040);
	#define RESTORE_DENORMALS SETCSR(_savemxcsr);
	
	#define ROUNDMODE_NEG_INF int _savemxcsr = GETCSR(); SETCSR((_savemxcsr & ~0x6000) | 0x2000);
	#define RESTORE_ROUNDMODE SETCSR(_savemxcsr);
	#define SET_ROUNDMODE 		ROUNDMODE_NEG_INF
#else
	#define DISABLE_DENORMALS
	#define RESTORE_DENORMALS
	
	#define ROUNDMODE_NEG_INF
	#define RESTORE_ROUNDMODE
	#define SET_ROUNDMODE 		ROUNDMODE_NEG_INF
#endif

// ____________________________________________________________________________
//
#if TARGET_OS_WIN32 && (TARGET_CPU_X86 || TARGET_CPU_X86_64)
	// x87 denormal handling
	inline double DenormalToZero(double f)
	{
		if (-1.0e-15 < f && f < 1.0e-15) {
			f = 0.0;
		}
		return f;
	}
	inline float DenormalToZero(float f)
	{
		if (-1.0e-15 < f && f < 1.0e-15) {
			f = 0.0f;
		}
		return f;
	}

#else
	// MacOS/X86 and PPC do not need this
	#define DenormalToZero(f) (f)
#endif

#endif
