////////////////////////////////////////////////////////////////////////////////
// Copyright 2017 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not
// use this file except in compliance with the License.  You may obtain a copy
// of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
// License for the specific language governing permissions and limitations
// under the License.
////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Common shared include file to hide compiler/os specific functions from the rest of the code. 
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if defined(_MSC_VER) && !defined(__INTEL_COMPILER) && !defined(__clang__)
	#define __MICROSOFT_COMPILER
#endif

#if defined(_WIN32)	&& (defined(_MSC_VER) || defined(__INTEL_COMPILER) || defined(__clang__)) // Windows: MSVC / Intel compiler / clang
	#include <intrin.h>
	#include <new.h>

	#define FORCE_INLINE __forceinline

	FORCE_INLINE unsigned long find_clear_lsb(unsigned int *mask)
	{
		unsigned long idx;
		_BitScanForward(&idx, *mask);
		*mask &= *mask - 1;
		return idx;
	}

	FORCE_INLINE void *aligned_alloc(size_t alignment, size_t size)
	{
		return _aligned_malloc(size, alignment);
	}

	FORCE_INLINE void aligned_free(void *ptr)
	{
		_aligned_free(ptr);
	}

#elif defined(__GNUG__)	|| defined(__clang__) // G++ or clang
	#include <cpuid.h>
#if defined(__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__)
	#include <malloc/malloc.h> // memalign
#else
	#include <malloc.h> // memalign
#endif
	#include <mm_malloc.h>
	#include <immintrin.h>
	#include <new>

	#define FORCE_INLINE inline

	FORCE_INLINE unsigned long find_clear_lsb(unsigned int *mask)
	{
		unsigned long idx;
		idx = __builtin_ctzl(*mask);
		*mask &= *mask - 1;
		return idx;
	}

	FORCE_INLINE void *aligned_alloc(size_t alignment, size_t size)
	{
		return memalign(alignment, size);
	}

	FORCE_INLINE void aligned_free(void *ptr)
	{
		free(ptr);
	}

	// GCC/Clang (via immintrin.h) may already define __cpuidex and _xgetbv.
	// Use __attribute__((unused)) overloads to suppress redefinition on Linux.
#if !defined(__GNUC__) || defined(_MSC_VER)
	FORCE_INLINE void __cpuidex(int* cpuinfo, int function, int subfunction)
	{
		__cpuid_count(function, subfunction, cpuinfo[0], cpuinfo[1], cpuinfo[2], cpuinfo[3]);
	}

	FORCE_INLINE unsigned long long _xgetbv(unsigned int index)
	{
		unsigned int eax, edx;
		__asm__ __volatile__(
			"xgetbv;"
			: "=a" (eax), "=d"(edx)
			: "c" (index)
		);
		return ((unsigned long long)edx << 32) | eax;
	}
#else
	// GCC provides __cpuidex via <cpuid.h> (transitively from <immintrin.h>).
	// __builtin_ia32_xgetbv is available as a GCC built-in.
	FORCE_INLINE unsigned long long _xgetbv_compat(unsigned int index)
	{
		return __builtin_ia32_xgetbv(index);
	}
	// Alias to the compat version so MOC internal code compiles.
	#define _xgetbv _xgetbv_compat
#endif

#else
	#error Unsupported compiler
#endif
