dnl Process this file with autoconf to produce a configure script
AC_INIT([trxcon], [0.0.0])
AM_INIT_AUTOMAKE

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

dnl Checks for typedefs, structures and compiler characteristics

AC_CONFIG_FILES([include/Makefile
		 include/osmocom/Makefile
		 include/osmocom/bb/Makefile
		 include/osmocom/bb/trxcon/Makefile
		 src/Makefile
		 Makefile])
AC_OUTPUT
