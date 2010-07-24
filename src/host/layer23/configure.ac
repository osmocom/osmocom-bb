dnl Process this file with autoconf to produce a configure script
AC_INIT

AM_INIT_AUTOMAKE(layer23, 0.0.0)

dnl kernel style compile messages
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

dnl checks for programs
AC_PROG_MAKE_SET
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_RANLIB

dnl checks for libraries
PKG_CHECK_MODULES(LIBOSMOCORE, libosmocore)
PKG_CHECK_MODULES(LIBOSMOVTY, libosmovty)

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
    Makefile)
