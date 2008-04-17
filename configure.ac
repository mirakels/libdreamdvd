AC_INIT([libdreamdvd],[0.0.1],[seddi@i-have-a-dreambox.com],[libdreamdvd])
AM_INIT_AUTOMAKE([dist-bzip2])

AC_PROG_CXX
AC_PROG_LIBTOOL
AC_CHECK_HEADERS([byteswap.h endian.h])

AC_ARG_WITH(dvbincludes,
	[  --with-dvbincludes=PATH  path for dvb includes [[NONE]]],
	[DVBINCLUDES="$withval"],[DVBINCLUDES=""])

if test "$DVBINCLUDES"; then
	CPPFLAGS="$CPPFLAGS -I$DVBINCLUDES"
fi

AC_CHECK_HEADERS(ost/dmx.h,[
	DVB_API_VERSION=1
	AC_MSG_NOTICE([found dvb version 1])
])

if test -z "$DVB_API_VERSION"; then
AC_CHECK_HEADERS(linux/dvb/version.h,[
	AC_LANG_PREPROC_REQUIRE()
	AC_REQUIRE([AC_PROG_EGREP])
	AC_LANG_CONFTEST([AC_LANG_SOURCE([[
#include <linux/dvb/version.h>
version DVB_API_VERSION
	]])])
	DVB_API_VERSION=`(eval "$ac_cpp conftest.$ac_ext") 2>&AS_MESSAGE_LOG_FD | $EGREP "^version" | sed "s,version\ ,,"`
	rm -f conftest*

	AC_MSG_NOTICE([found dvb version $DVB_API_VERSION])
])
fi

if test "$DVB_API_VERSION"; then
	AC_DEFINE(HAVE_DVB,1,[Define to 1 if you have the dvb includes])
	AC_DEFINE_UNQUOTED(HAVE_DVB_API_VERSION,$DVB_API_VERSION,[Define to the version of the dvb api])
else
	AC_MSG_ERROR([can't find dvb headers])
fi

AC_OUTPUT([
Makefile
libdreamdvd.pc
])
