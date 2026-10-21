Vendored talloc
===============

This directory contains the talloc sources that PRoot links against.

talloc is licensed under the GNU Lesser General Public License, version 3
or later.  The upstream project is at https://www.samba.org/talloc/ and
the source in this directory is talloc 2.4.2.

Why it is committed instead of downloaded
-----------------------------------------

The build downloads PRoot's own sources, but talloc is not fetched from
the network.  The upstream tarball was unreliable from CI runners, and
waf's configure step cannot run target binaries on a cross build, so
the tree is committed instead.  config.h is a pre-generated, sanitised
copy of the one waf produces; it was checked in with the values that
match a Linux host build.

The files here are:

  talloc.c        the library
  talloc.h        the public header
  config.h        pre-generated build configuration
  lib/replace/    the replacement headers for functions talloc can
                  substitute for the system ones

To update, replace the tree with a newer upstream release and re-check
config.h against the values waf would produce.
