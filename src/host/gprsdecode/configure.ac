dnl Process this file with autoconf to produce a configure script
AC_INIT([gprsdecode], [0.0.0])
AM_INIT_AUTOMAKE

CFLAGS="$CFLAGS -std=gnu11"

dnl kernel style compile messages
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

dnl Tests
AC_CONFIG_TESTDIR(tests)

dnl checks for programs
AC_PROG_MAKE_SET
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_RANLIB

dnl checks for libraries
PKG_CHECK_MODULES(LIBOSMOCORE, libosmocore)
PKG_CHECK_MODULES(LIBOSMOGSM, libosmogsm)
PKG_CHECK_MODULES(LIBOSMOCODING, libosmocoding)

dnl checks for header files
AC_HEADER_STDC

dnl generates the output files
AC_OUTPUT(
	Makefile
	tests/Makefile
)
