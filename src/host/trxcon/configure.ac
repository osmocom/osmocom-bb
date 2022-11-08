dnl Process this file with autoconf to produce a configure script
AC_INIT([trxcon], [0.0.0])
AM_INIT_AUTOMAKE

CFLAGS="$CFLAGS -std=gnu11"

dnl kernel style compile messages
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

dnl checks for programs
AC_PROG_MAKE_SET
AC_PROG_CC
AC_PROG_INSTALL

dnl checks for libraries
PKG_CHECK_MODULES(LIBOSMOCORE, libosmocore)
PKG_CHECK_MODULES(LIBOSMOCODING, libosmocoding)
PKG_CHECK_MODULES(LIBOSMOGSM, libosmogsm)

dnl checks for header files
AC_HEADER_STDC

dnl init libtool
LT_INIT

AC_ARG_ENABLE(sanitize,
	[AS_HELP_STRING(
		[--enable-sanitize],
		[Compile with address sanitizer enabled],
	)], [sanitize=$enableval], [sanitize="no"])
if test x"$sanitize" = x"yes"
then
	CFLAGS="$CFLAGS -fsanitize=address -fsanitize=undefined"
	CPPFLAGS="$CPPFLAGS -fsanitize=address -fsanitize=undefined"
fi

AC_ARG_ENABLE(werror,
	[AS_HELP_STRING(
		[--enable-werror],
		[Turn all compiler warnings into errors, with exceptions:
		 a) deprecation (allow upstream to mark deprecation without breaking builds);
		 b) "#warning" pragmas (allow to remind ourselves of errors without breaking builds)
		]
	)],
	[werror=$enableval], [werror="no"])
if test x"$werror" = x"yes"
then
	WERROR_FLAGS="-Werror"
	WERROR_FLAGS+=" -Werror=implicit-int -Werror=int-conversion -Werror=old-style-definition"
	WERROR_FLAGS+=" -Wno-error=deprecated -Wno-error=deprecated-declarations"
	WERROR_FLAGS+=" -Wno-error=cpp" # "#warning"
	CFLAGS="$CFLAGS $WERROR_FLAGS"
	CPPFLAGS="$CPPFLAGS $WERROR_FLAGS"
fi

AC_MSG_RESULT([CFLAGS="$CFLAGS"])
AC_MSG_RESULT([CPPFLAGS="$CPPFLAGS"])

dnl Checks for typedefs, structures and compiler characteristics

AC_CONFIG_MACRO_DIRS([m4])
AC_CONFIG_FILES([include/Makefile
		 include/osmocom/Makefile
		 include/osmocom/bb/Makefile
		 include/osmocom/bb/l1sched/Makefile
		 include/osmocom/bb/trxcon/Makefile
		 src/Makefile
		 Makefile])
AC_OUTPUT
