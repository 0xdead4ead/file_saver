dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT([file_saver], [1.0], [cplusplus@0xdead4ead.com])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([m4])

AC_CANONICAL_HOST
AM_INIT_AUTOMAKE([tar-ustar])

AC_PROG_CXX
AC_LANG(C++)
AX_BOOST_BASE([1.76.0], [], [
  AC_MSG_ERROR([Boost is not found!])])

AC_CONFIG_FILES([Makefile src/Makefile])
AC_OUTPUT
