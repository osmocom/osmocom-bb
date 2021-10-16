dnl Process this file with autoconf to produce a configure script
AC_INIT([layer23], [0.0.0])
AM_INIT_AUTOMAKE

dnl kernel style compile messages
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

dnl checks for programs
AC_PROG_MAKE_SET
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_RANLIB

AC_ARG_ENABLE(sanitize,
		[AS_HELP_STRING([--enable-sanitize], [Compile with address sanitizer enabled], )],
		[sanitize=$enableval], [sanitize="no"])
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
	WERROR_FLAGS+=" -Wno-error=deprecated -Wno-error=deprecated-declarations"
	WERROR_FLAGS+=" -Wno-error=cpp" # "#warning"
	CFLAGS="$CFLAGS $WERROR_FLAGS"
	CPPFLAGS="$CPPFLAGS $WERROR_FLAGS"
fi

dnl checks for libraries
PKG_CHECK_MODULES(LIBOSMOCORE, libosmocore >= 1.5.0)
PKG_CHECK_MODULES(LIBOSMOVTY, libosmovty >= 0.10.0)
PKG_CHECK_MODULES(LIBOSMOGSM, libosmogsm)
PKG_CHECK_MODULES(LIBOSMOCODEC, libosmocodec)
AC_CHECK_LIB(gps, gps_waiting, LIBGPS_CFLAGS=" -D_HAVE_GPSD" LIBGPS_LIBS=" -lgps ",,)
AC_SUBST([LIBGPS_CFLAGS])
AC_SUBST([LIBGPS_LIBS])


dnl optional dependencies
PKG_CHECK_MODULES(LIBLUA, lua53, [
	WITH_LUA=1], [
	WITH_LUA=0])
AC_SUBST([WITH_LUA])
AM_CONDITIONAL([BUILD_LUA], test "x$WITH_LUA" = "x1")

dnl checks for header files
AC_HEADER_STDC

dnl Checks for typedefs, structures and compiler characteristics

AC_OUTPUT(
    src/Makefile
    src/common/Makefile
    src/misc/Makefile
    src/mobile/Makefile
    include/Makefile
    include/osmocom/Makefile
    include/osmocom/bb/Makefile
    include/osmocom/bb/common/Makefile
    include/osmocom/bb/misc/Makefile
    include/osmocom/bb/mobile/Makefile
    Makefile)
