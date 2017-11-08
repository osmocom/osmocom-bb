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

dnl checks for libraries
PKG_CHECK_MODULES(LIBOSMOCORE, libosmocore)
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
