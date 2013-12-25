dnl Process this file with autoconf to produce a configure script
AC_INIT

AM_INIT_AUTOMAKE(layer23, 0.0.0)

dnl kernel style compile messages
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

dnl checks for programs
AC_PROG_MAKE_SET
AC_PROG_CC
AC_PROG_INSTALL
dnl AC_PROG_RANLIB
LT_INIT
AC_PROG_LIBTOOL

dnl checks for libraries
PKG_CHECK_MODULES(LIBOSMOCORE, libosmocore)
PKG_CHECK_MODULES(LIBOSMOVTY, libosmovty)
PKG_CHECK_MODULES(LIBOSMOGSM, libosmogsm)
PKG_CHECK_MODULES(LIBOSMOCODEC, libosmocodec)
AC_CHECK_LIB(gps, gps_waiting, LIBGPS_CFLAGS=" -D_HAVE_GPSD" LIBGPS_LIBS=" -lgps ",,)
AC_SUBST([LIBGPS_CFLAGS])
AC_SUBST([LIBGPS_LIBS])


dnl checks for header files
AC_HEADER_STDC

dnl Checks for typedefs, structures and compiler characteristics

AC_OUTPUT(
    src/Makefile
    src/common/Makefile
    dnl src/misc/Makefile
    dnl src/mobile/Makefile
    src/libosmosim/Makefile
    include/Makefile
    include/osmocom/Makefile
    include/osmocom/bb/Makefile
    include/osmocom/bb/common/Makefile
    include/osmocom/bb/misc/Makefile
    include/osmocom/bb/mobile/Makefile
    Makefile)
