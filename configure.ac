#
# Copyright (C) 2004-2010 Loic Dachary <loic@dachary.org>
# Copyright (C) 2008      Bradley M. Kuhn <bkuhn@ebb.org>
# Copyright (C) 2004-2006 Mekensleep <licensing@mekensleep.com>
#                         24 rue vieille du temple, 75004 Paris
#
# This program gives you software freedom; you can copy, convey,
# propagate, redistribute and/or modify this program under the terms of
# the GNU General Public License (GPL) as published by the Free Software
# Foundation (FSF), either version 3 of the License, or (at your option)
# any later version of the GPL published by the FSF.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program in a file in the toplevel directory called "GPLv3".
# If not, see <http://www.gnu.org/licenses/>.
#
# Authors:
#  Loic Dachary <loic@dachary.org>
#  Bradley M. Kuhn <bkuhn@ebb.org>

# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.71])
AC_INIT([poker-eval],[138.0])
AC_CONFIG_AUX_DIR(config)
AC_CANONICAL_TARGET
AM_INIT_AUTOMAKE()
AM_CONFIG_HEADER([include/config.h include/poker_config.h])
AC_CONFIG_SRCDIR([lib/deck.c])

# Checks for programs.
AC_PROG_MAKE_SET
AC_USE_SYSTEM_EXTENSIONS
AC_PROG_CC
LT_INIT
AC_PATH_PROG([AWK], [awk])

# Mac OS X uses md5 instead of md5sum
AC_PATH_PROG([MD5SUM], [md5sum])
if test x"$MD5SUM" = x ; then
  AC_PATH_PROG([MD5SUM], [md5])
fi

AC_PATH_PROG([VALGRIND], [valgrind])

if test "$GCC" = "yes" ; then
  CPPFLAGS="$CPPFLAGS -Wall -Wpointer-arith -Wstrict-prototypes"
fi

AC_EXEEXT
AC_OBJEXT
AC_SEARCH_LIBS([strerror],[cposix])
m4_warn([obsolete],
[The preprocessor macro `STDC_HEADERS' is obsolete.
  Except in unusual embedded environments, you can safely include all
  ISO C90 headers unconditionally.])dnl
# Autoupdate added the next two lines to ensure that your configure
# script's behavior did not change.  They are probably safe to remove.
AC_CHECK_INCLUDES_DEFAULT
AC_PROG_EGREP

AC_C_INLINE

AC_C_CONST
AC_C_INLINE
AC_C_BIGENDIAN
AM_PROG_CC_C_O
AC_CHECK_SIZEOF(long)
AC_CHECK_HEADERS(unistd.h)
AC_CHECK_HEADERS(sys/stat.h)
AC_CHECK_TYPES([long long, uint64_t, int8])

AM_PATH_CCACHE
AM_GCOV

# Generate files
AC_CONFIG_FILES([tests/run], [chmod 544 tests/run])
AC_CONFIG_FILES([Makefile
	         poker-eval.pc
		 poker-eval.spec
                 lib/Makefile
                 include/Makefile
                 examples/Makefile
                 tests/Makefile
	 ])
AC_OUTPUT
