#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_INIT([libdreamdvd],[0.0.1],[enigma2-devel@lists.elitedvb.net])
AM_INIT_AUTOMAKE([dist-bzip2 foreign])
AC_CONFIG_SRCDIR([main.c])
AC_CONFIG_HEADERS([libdreamdvd_config.h])

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_USE_SYSTEM_EXTENSIONS
AC_SYS_LARGEFILE

# Checks for programs.
AC_PROG_CC
m4_ifdef([LT_INIT], [LT_INIT], [AC_PROG_LIBTOOL])
AC_CONFIG_MACRO_DIR([m4])

# Checks for libraries.
PKG_CHECK_MODULES(DVDNAV, dvdnav)
AC_CHECK_LIB([dl], [dlopen], [LIBDL_LIBS="-ldl"], [AC_MSG_ERROR([Could not find libdl])])
AC_SUBST(LIBDL_LIBS)
AC_CHECK_LIB([m], [pow], [LIBM_LIBS="-lm"], [AC_MSG_ERROR([Could not find libm])])
AC_SUBST(LIBM_LIBS)

# Checks for header files.
AC_CHECK_HEADERS([byteswap.h ost/dmx.h linux/dvb/version.h])

AC_CONFIG_FILES([
Makefile
libdreamdvd.pc
])
AC_OUTPUT
