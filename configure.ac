AC_INIT([libosmocore],
	m4_esyscmd([./git-version-gen .tarball-version]),
	[openbsc-devel@lists.openbsc.org])

AM_INIT_AUTOMAKE([dist-bzip2])

dnl kernel style compile messages
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

dnl checks for programs
AC_PROG_MAKE_SET
AC_PROG_CC
AC_PROG_INSTALL
LT_INIT
AC_PROG_LIBTOOL

AC_CONFIG_MACRO_DIR([m4])

dnl checks for header files
AC_HEADER_STDC
AC_CHECK_HEADERS(execinfo.h sys/select.h sys/socket.h syslog.h ctype.h)

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
	[enable_talloc=0], [enable_talloc=1])
AM_CONDITIONAL(ENABLE_TALLOC, test "x$enable_talloc" = "x1")

AC_ARG_ENABLE(plugin,
	[AS_HELP_STRING(
		[--disable-plugin],
		[Disable support for dlopen plugins],
	)],
	[enable_plugin=0], [enable_plugin=1])
AM_CONDITIONAL(ENABLE_PLUGIN, test "x$enable_plugin" = "x1")

AC_ARG_ENABLE(tests,
	[AS_HELP_STRING(
		[--disable-tests],
		[Disable building test programs]
	)],
	[enable_tests=0], [enable_tests=1])
AM_CONDITIONAL(ENABLE_TESTS, test "x$enable_tests" = "x1")

AC_ARG_ENABLE(vty,
	[AS_HELP_STRING(
		[--disable-vty],
		[Disable building VTY telnet interface]
	)],
	[enable_vty=0], [enable_vty=1])
AM_CONDITIONAL(ENABLE_VTY, test "x$enable_vty" = "x1")

AC_ARG_ENABLE(panic_infloop,
	[AS_HELP_STRING(
		[--enable-panic-infloop],
		[Trigger infinite loop on panic rather than fprintf/abort]
	)],
	[panic_infloop=1], [panic_infloop=0])
if test "x$panic_infloop" = "x1"
then
	AC_DEFINE([PANIC_INFLOOP],[1],[Use infinite loop on panic rather than fprintf/abort])
fi

AC_ARG_ENABLE(bsc_fd_check,
	[AS_HELP_STRING(
		[--enable-bsc-fd-check],
		[Instrument bsc_register_fd to check that the fd is registered]
	)],
	[fd_check=1], [fd_check=0])
if test "x$fd_check" = "x1"
then
	AC_DEFINE([BSC_FD_CHECK],[1],[Instrument the bsc_register_fd])
fi

AC_ARG_ENABLE(msgfile,
	[AS_HELP_STRING(
		[--disable-msgfile],
		[Disable support for the msgfile],
	)],
	[enable_msgfile=0], [enable_msgfile=1])
AM_CONDITIONAL(ENABLE_MSGFILE, test "x$enable_msgfile" = "x1")


AC_OUTPUT(
	libosmocore.pc
	libosmocodec.pc
	libosmovty.pc
	libosmogsm.pc
	include/osmocom/Makefile
	include/osmocom/vty/Makefile
	include/osmocom/codec/Makefile
	include/osmocom/crypt/Makefile
	include/osmocom/gsm/Makefile
	include/osmocom/gsm/protocol/Makefile
	include/osmocom/core/Makefile
	include/Makefile
	src/Makefile
	src/vty/Makefile
	src/codec/Makefile
	src/gsm/Makefile
	tests/Makefile
	tests/timer/Makefile
	tests/sms/Makefile
	tests/msgfile/Makefile
	tests/ussd/Makefile
	tests/smscb/Makefile
	utils/Makefile
	Doxyfile.core
	Doxyfile.gsm
	Doxyfile.vty
	Doxyfile.codec
	Makefile)
