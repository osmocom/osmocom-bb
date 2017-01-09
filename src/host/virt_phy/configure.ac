dnl Process this file with autoconf to produce a configure script
AC_INIT([virtphy], 0.0.0)
AM_CONFIG_HEADER([config.h])
AM_INIT_AUTOMAKE([foreign dist-bzip2])

dnl kernel style compile messages
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

dnl checks for programs
AC_PROG_MAKE_SET
AC_PROG_CC
AC_PROG_INSTALL

dnl checks for libraries
PKG_CHECK_MODULES(LIBOSMOCORE, libosmocore)
PKG_CHECK_MODULES(LIBOSMOGSM, libosmogsm)

dnl checks for header files
AC_HEADER_STDC

dnl Checks for typedefs, structures and compiler characteristics

AC_CONFIG_FILES([
 Makefile
 src/Makefile
])
AC_OUTPUT
