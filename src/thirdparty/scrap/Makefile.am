noinst_LIBRARIES = libscrap.a
libscrap_a_SOURCES = scrap.cpp scrap.h

EXTRA_DIST = COPYING

# Disable SIMD feature macros so SDL does not include pmmintrin.h / ia32intrin.h.
# GCC 16 has an ICE in those headers when compiled with -O2 + -std=c++23 (pragma GCC pop_options
# triggers a segfault inside cl_optimization_restore). scrap.cpp needs no SIMD.
libscrap_a_CXXFLAGS = ${AM_CXXFLAGS} -I${top_srcdir}/src -I./ \
	-mno-sse3 -mno-ssse3 -mno-sse4.1 -mno-sse4.2 -mno-avx -mno-avx2
