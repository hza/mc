dnl
dnl libarchive, used to create archives from the Copy dialog
dnl
dnl if --with-libarchive is specified and the library is not found, configure fails.
dnl If it is not specified and the library is not found, configure continues with a warning.

AC_DEFUN([mc_LIBARCHIVE],
[
    LIBARCHIVE_MIN_VERSION="3.3"

    AC_ARG_WITH([libarchive],
        AS_HELP_STRING([--with-libarchive],
        [Create archives with libarchive @<:@auto@:>@]),
        [with_libarchive=$withval],
        [with_libarchive=auto])

    libarchive_msg="no"

    if test "x$with_libarchive" != xno; then
        PKG_CHECK_MODULES(LIBARCHIVE, [libarchive >= $LIBARCHIVE_MIN_VERSION],
                          [found_libarchive=yes], [:])

        if test x"$found_libarchive" != "xyes"; then
            AC_CHECK_HEADER([archive.h],
                [AC_CHECK_LIB([archive], [archive_write_new],
                    [found_libarchive=yes; LIBARCHIVE_CFLAGS=""; LIBARCHIVE_LIBS="-larchive"])])
        fi

        if test x"$found_libarchive" = "xyes"; then
            libarchive_msg="yes"
            AC_DEFINE(HAVE_LIBARCHIVE, 1, [Define to create archives with libarchive])
            MCLIBS="$MCLIBS $LIBARCHIVE_LIBS"
            CPPFLAGS="$CPPFLAGS $LIBARCHIVE_CFLAGS"
        elif test "x$with_libarchive" = xyes; then
            AC_MSG_ERROR([libarchive $LIBARCHIVE_MIN_VERSION or newer not found])
        else
            AC_MSG_WARN([libarchive not found, archives cannot be created])
        fi
    fi
])
