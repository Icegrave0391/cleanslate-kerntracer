# Makefile.am --- automake input file for systemtap runtime tools

AUTOMAKE_OPTIONS = subdir-objects

AM_CFLAGS = -Wall -Wextra -Wunused -W -Wformat=2
AM_CXXFLAGS = -Wall -Wextra -Wunused -W -Wformat=2
if Werror
AM_CFLAGS += -Werror
AM_CXXFLAGS += -Werror
endif
AM_CPPFLAGS = -D_GNU_SOURCE
AM_CPPFLAGS += -I$(srcdir)/../includes
AM_CPPFLAGS += -I$(builddir)/../includes/sys
AM_CPPFLAGS += -DBINDIR='"$(bindir)"' -DSYSCONFDIR='"$(sysconfdir)"' -DPKGDATADIR='"${pkgdatadir}"' -DPKGLIBDIR='"$(pkglibexecdir)"' -DLOCALEDIR='"$(localedir)"'

AM_CFLAGS += @PIECFLAGS@
AM_CXXFLAGS += @PIECXXFLAGS@
AM_LDFLAGS = @PIELDFLAGS@

bin_PROGRAMS = staprun stap-merge stapsh
pkglibexec_PROGRAMS = stapio

# Tighten -Wno-format-nonliteral to just where it's needed.
# See the automake manual secton: "Per-Object Flags Emulation"
#   https://www.gnu.org/software/automake/manual/html_node/Per_002dObject-Flags.html
noinst_LIBRARIES = libstrfloctime.a
libstrfloctime_a_SOURCES = strfloctime.c
libstrfloctime_a_CFLAGS = $(AM_CFLAGS) -Wno-format-nonliteral

staprun_SOURCES = staprun.c staprun_funcs.c ctl.c common.c start_cmd.c \
	../privilege.cxx ../staputil.cxx
staprun_CFLAGS = $(AM_CFLAGS) -DSINGLE_THREADED $(debuginfod_CFLAGS)
staprun_CXXFLAGS = $(AM_CXXFLAGS) -DSINGLE_THREADED $(debuginfod_CFLAGS)
staprun_CPPFLAGS = $(AM_CPPFLAGS)
staprun_LDADD = libstrfloctime.a $(staprun_LIBS) $(debuginfod_LIBS)
staprun_LDFLAGS =  $(AM_LDFLAGS) -Wl,--whole-archive,libstrfloctime.a,--no-whole-archive $(debuginfod_LDFLAGS)

if HAVE_NSS
staprun_SOURCES += modverify.c ../nsscommon.cxx
staprun_CFLAGS += $(nss_CFLAGS)
staprun_CXXFLAGS += $(nss_CFLAGS)
staprun_LDADD += $(nss_LIBS)
endif

if HAVE_HTTP_SUPPORT
staprun_LDADD += $(openssl_LIBS)
endif

stapio_SOURCES = stapio.c mainloop.c common.c start_cmd.c ctl.c relay.c monitor.c
stapio_LDADD =  libstrfloctime.a -lpthread
stapio_LDFLAGS =  -Wl,--whole-archive,libstrfloctime.a,--no-whole-archive

if HAVE_MONITOR_LIBS
stapio_LDADD += $(jsonc_LIBS) -lpanel $(ncurses_LIBS)
endif

man_MANS = staprun.8

stap_merge_SOURCES = stap_merge.c
stap_merge_CFLAGS = $(AM_CFLAGS)
stap_merge_LDFLAGS = $(AM_LDFLAGS)
stap_merge_LDADD =

stapsh_SOURCES = stapsh.c
stapsh_CFLAGS = $(AM_CFLAGS)
stapsh_LDFLAGS = $(AM_LDFLAGS)
stapsh_LDADD =

BUILT_SOURCES =
CLEANFILES =

# Arrange for the top-level git_version.h to be regenerated at every "make".
BUILT_SOURCES += git_version.stamp
git_version.stamp ../git_version.h:
	$(MAKE) -C .. $(notdir $@)


# Why the "id -u" condition?  This way, an unprivileged user can run
# make install, and have "sudo stap ...." or "sudo staprun ...." work later.
install-exec-hook:
	if [ `id -u` -eq 0 ] && (getent group stapusr >/dev/null \
	                         || groupadd -f -g 156 -r stapusr); then \
		chgrp stapusr "$(DESTDIR)$(bindir)/staprun" \
		&& chmod 04110 "$(DESTDIR)$(bindir)/staprun"; \
	fi
