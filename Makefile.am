lib_LTLIBRARIES = libdreamdvd.la
libdreamdvd_la_CFLAGS = @LIBDVDNAV_CFLAGS@ @LIBDVDREAD_CFLAGS@
libdreamdvd_la_SOURCES = main.c mpegaudio_enc.c a52_dec.c
libdreamdvd_la_LDFLAGS = @LIBDVDNAV_LIBS@ @LIBDVDREAD_LIBS@ 

pkgincludedir = ${includedir}/dreamdvd
pkginclude_HEADERS = ddvdlib.h
pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libdreamdvd.pc