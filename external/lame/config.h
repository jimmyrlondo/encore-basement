/* Basement's config.h for LAME's libmp3lame (stands in for the one
   ./configure would write). Plain C, no assembly or SSE paths, so the same
   build works on Windows, Intel Macs and Apple Silicon. */
#ifndef BASEMENT_LAME_CONFIG_H
#define BASEMENT_LAME_CONFIG_H

#define STDC_HEADERS 1
#define HAVE_ERRNO_H 1
#define HAVE_FCNTL_H 1
#define HAVE_LIMITS_H 1
#define HAVE_STDINT_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_STRCHR 1
#define HAVE_MEMCPY 1
#define PROTOTYPES 1
#define USE_FAST_LOG 1
#define PACKAGE "lame"
#define LAME_LIBRARY_BUILD 1

typedef long double ieee854_float80_t;
typedef double      ieee754_float64_t;
typedef float       ieee754_float32_t;

#ifdef _MSC_VER
 #pragma warning (disable : 4018 4244 4267 4305 4996)
#endif

#endif
