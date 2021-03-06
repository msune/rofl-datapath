AC_INIT(ROFL-DATAPATH, m4_esyscmd_s(cat VERSION), rofl-devel@roflibs.org, rofl-datapath, http://www.roflibs.org)

AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE([-Wall -Werror foreign subdir-objects])

AC_GNU_SOURCE

AC_PROG_INSTALL
AC_PROG_CC
AC_PROG_CXX
AC_PROG_LD
AM_PROG_CC_C_O
m4_ifdef([AM_PROG_AR], [AM_PROG_AR])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

PKG_PROG_PKG_CONFIG

AUTOCONF_ENV="SHELL=/bin/sh"

LT_INIT
AC_ENABLE_STATIC

# default flags
CFLAGS="-std=gnu89 -Werror -Wall $CFLAGS"
CXXFLAGS="-Werror -Wall $CXXFLAGS"
AC_DEFINE([__STDC_FORMAT_MACROS], [], [Description])


# Perform GCC checkings
m4_include([config/gcc.m4])

# Debug
m4_include([config/debug.m4])

# Endianness
m4_include([config/endianness.m4])

# Check for profiling mode
m4_include([config/profiling.m4])

# Experimental
m4_include([config/experimental.m4])

# Pipeline support
m4_include([config/pipeline.m4])

# Pipeline optimizations
m4_include([config/pipeline_opts.m4])

# Checking libs
m4_include([config/versioning.m4])

# Output files
AC_CONFIG_FILES([

	Makefile
	src/Makefile
	src/rofl/Makefile
	src/rofl/rofl_datapath.pc

	src/rofl/datapath/hal/Makefile
	src/rofl/datapath/hal/openflow/openflow1x/Makefile
	src/rofl/datapath/hal/extensions/Makefile

	test/Makefile

	src/rofl_datapath_conf.h
])

# Doxygen (here to be the last Makefile)
m4_include([config/doxygen.m4])

AC_OUTPUT

