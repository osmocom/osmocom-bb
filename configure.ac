AC_INIT([libosmocore],
	m4_esyscmd([./git-version-gen .tarball-version]),
	[openbsc@lists.osmocom.org])

AM_INIT_AUTOMAKE([dist-bzip2])
AC_CONFIG_TESTDIR(tests)

dnl kernel style compile messages
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

dnl checks for programs
AC_PROG_MAKE_SET
AC_PROG_CC
AC_PROG_INSTALL
LT_INIT
AC_PROG_LIBTOOL
CHECK_GCC_FVISIBILITY

AC_CONFIG_MACRO_DIR([m4])

dnl checks for header files
AC_HEADER_STDC
AC_CHECK_HEADERS(execinfo.h sys/select.h sys/socket.h syslog.h ctype.h)
# for src/conv.c
AC_FUNC_ALLOCA
AC_SEARCH_LIBS([dlopen], [dl dld], [LIBRARY_DL="$LIBS";LIBS=""])
AC_SUBST(LIBRARY_DL)

AC_PATH_PROG(DOXYGEN,doxygen,false)
AM_CONDITIONAL(HAVE_DOXYGEN, test $DOXYGEN != false)

# The following test is taken from WebKit's webkit.m4
saved_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS -fvisibility=hidden "
AC_MSG_CHECKING([if ${CC} supports -fvisibility=hidden])
AC_COMPILE_IFELSE([AC_LANG_SOURCE([char foo;])],
      [ AC_MSG_RESULT([yes])
        SYMBOL_VISIBILITY="-fvisibility=hidden"],
        AC_MSG_RESULT([no]))
CFLAGS="$saved_CFLAGS"
AC_SUBST(SYMBOL_VISIBILITY)

dnl Generate the output
AM_CONFIG_HEADER(config.h)

AC_ARG_ENABLE(talloc,
	[AS_HELP_STRING(
		[--disable-talloc],
		[Disable building talloc memory allocator]
	)],
	[enable_talloc=$enableval], [enable_talloc="yes"])
AM_CONDITIONAL(ENABLE_TALLOC, [test x"$enable_talloc" = x"yes"])

AC_ARG_ENABLE(plugin,
	[AS_HELP_STRING(
		[--disable-plugin],
		[Disable support for dlopen plugins],
	)],
	[enable_plugin=$enableval], [enable_plugin="yes"])
AM_CONDITIONAL(ENABLE_PLUGIN, test x"$enable_plugin" = x"yes")

AC_ARG_ENABLE(vty,
	[AS_HELP_STRING(
		[--disable-vty],
		[Disable building VTY telnet interface]
	)],
	[enable_vty=$enableval], [enable_vty="yes"])
AM_CONDITIONAL(ENABLE_VTY, test x"$enable_vty" = x"yes")

AC_ARG_ENABLE(panic_infloop,
	[AS_HELP_STRING(
		[--enable-panic-infloop],
		[Trigger infinite loop on panic rather than fprintf/abort]
	)],
	[panic_infloop=$enableval], [panic_infloop="no"])
if test x"$panic_infloop" = x"yes"
then
	AC_DEFINE([PANIC_INFLOOP],[1],[Use infinite loop on panic rather than fprintf/abort])
fi

AC_ARG_ENABLE(bsc_fd_check,
	[AS_HELP_STRING(
		[--enable-bsc-fd-check],
		[Instrument bsc_register_fd to check that the fd is registered]
	)],
	[fd_check=$enableval], [fd_check="no"])
if test x"$fd_check" = x"no"
then
	AC_DEFINE([BSC_FD_CHECK],[1],[Instrument the bsc_register_fd])
fi

AC_ARG_ENABLE(msgfile,
	[AS_HELP_STRING(
		[--disable-msgfile],
		[Disable support for the msgfile],
	)],
	[enable_msgfile=$enableval], [enable_msgfile="yes"])
AM_CONDITIONAL(ENABLE_MSGFILE, test x"$enable_msgfile" = x"yes")

AC_ARG_ENABLE(serial,
	[AS_HELP_STRING(
		[--disable-serial],
		[Disable support for the serial helpers],
	)],
	[enable_serial=$enableval], [enable_serial="yes"])
AM_CONDITIONAL(ENABLE_SERIAL, test x"$enable_serial" = x"yes")

AC_ARG_ENABLE(utilities,
        [AS_HELP_STRING(
                [--disable-utilities],
                [Disable building utility programs],
        )],
        [enable_utilities=$enableval], [enable_utilities="yes"])
AM_CONDITIONAL(ENABLE_UTILITIES, test x"$enable_utilities" = x"yes")

AC_ARG_ENABLE(embedded,
	[AS_HELP_STRING(
		[--enable-embedded],
		[Enable building for embedded use and disable unsupported features]
	)],
	[embedded=$enableval], [embedded="no"])
if test x"$embedded" = x"yes"
then
	AC_DEFINE([EMBEDDED],[1],[Select building for embedded use])
	AM_CONDITIONAL(ENABLE_PLUGIN, false)
	AM_CONDITIONAL(ENABLE_MSGFILE, false)
	AM_CONDITIONAL(ENABLE_SERIAL, false)
	AM_CONDITIONAL(ENABLE_VTY, false)
	AM_CONDITIONAL(ENABLE_TALLOC, false)
	AM_CONDITIONAL(ENABLE_UTILITIES, false)
	AC_DEFINE([PANIC_INFLOOP],[1],[Use infinite loop on panic rather than fprintf/abort])
fi


AC_OUTPUT(
	libosmocore.pc
	libosmocodec.pc
	libosmovty.pc
	libosmogsm.pc
	libosmogb.pc
	include/osmocom/Makefile
	include/osmocom/vty/Makefile
	include/osmocom/codec/Makefile
	include/osmocom/crypt/Makefile
	include/osmocom/gsm/Makefile
	include/osmocom/gsm/protocol/Makefile
	include/osmocom/gprs/Makefile
	include/osmocom/gprs/protocol/Makefile
	include/osmocom/core/Makefile
	include/Makefile
	src/Makefile
	src/vty/Makefile
	src/codec/Makefile
	src/gsm/Makefile
	src/gb/Makefile
	tests/Makefile
	utils/Makefile
	Doxyfile.core
	Doxyfile.gsm
	Doxyfile.vty
	Doxyfile.codec
	Makefile)
