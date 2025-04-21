# Makefile.am --- automake input file for systemtap testsuite
## process this file with automake to produce Makefile.in

AUTOMAKE_OPTIONS = dejagnu no-dist subdir-objects

all-local:
	@echo Run \"make check\" or \"make installcheck\".

clean-local:
	-rm -f ./stap site.exp systemtap.sum systemtap.log
	-rm -f systemtap.temp*
	-rm -rf .systemtap* .cache_test* 2>/dev/null
	-rm -f ./stap_*.ko
	-rm -f flightrec*
	-rm -f *.so

TESTAPPS=@enable_testapps@

# but installcheck does not follow an implicit check-DEJAGNU, go figure
installcheck: site.exp
	$(MAKE) $(AM_MAKEFLAGS) check-DEJAGNU RUNTESTFLAGS="$(RUNTESTFLAGS) --tool_opts install"


# There are a few tests known to be slow. We run them right from the beginning
# to try to lessen the overall time taken by the test suite -- if one of these
# tests happens to be run late, it will cause the overall time to increase.
all_slow_tests = systemtap.examples/check.exp systemtap.pass1-4/buildok.exp \
	systemtap.base/listing_mode.exp systemtap.syscall/nd_syscall.exp \
	systemtap.unprivileged/unprivileged_myproc.exp systemtap.syscall/syscall.exp \
	systemtap.base/sdt_misc.exp systemtap.pass1-4/parseok.exp \
	systemtap.base/listing_mode_sanity.exp systemtap.pass1-4/parse-semok.exp \
	systemtap.pass1-4/semok.exp systemtap.unprivileged/unprivileged_probes.exp \
	systemtap.pass1-4/buildok-interactive.exp \
	systemtap.onthefly/uprobes_onthefly.exp \
	systemtap.onthefly/kprobes_onthefly.exp systemtap.base/sdt.exp \
	systemtap.onthefly/hrtimer_onthefly.exp \
	systemtap.onthefly/tracepoint_onthefly.exp systemtap.exelib/exelib.exp \
	systemtap.pass1-4/semko.exp systemtap.pass1-4/parseko.exp \
	systemtap.base/rlimit.exp systemtap.base/tracepoints.exp \
	systemtap.base/systemtap-service.exp systemtap.maps/map_hash.exp \
	systemtap.base/stmt_rel.exp systemtap.base/library.exp
slow_tests = $(filter $(all_tests),$(all_slow_tests))

regular_tests := $(filter-out $(serial_tests) $(slow_tests) ,$(all_tests))

REGULAR_TEST_TARGETS := $(addprefix installcheck/,$(slow_tests) $(regular_tests))


# $(srcdir)/These values point the test suite to the install tree, and
# are overridden by "make check" from full source/build tree
SYSTEMTAP_RUNTIME=$(DESTDIR)$(pkgdatadir)/runtime
SYSTEMTAP_TAPSET=$(DESTDIR)$(pkgdatadir)/tapset
CRASH_LIBDIR=$(DESTDIR)$(libdir)/systemtap
SYSTEMTAP_PATH=$(DESTDIR)$(bindir)
SYSTEMTAP_SPATH=$(DESTDIR)$(sbindir)
SYSTEMTAP_INCLUDES=$(DESTDIR)$(includedir)

# These values get passed down from the main configure.
PYTHON=@python@
PYEXECDIR=@pyexecdir@
PYTHON3=@python3@
PY3EXECDIR=@py3execdir@
SYSCONFDIR=@sysconfdir@

RUNTESTDEFAULTFLAGS = --tool $$tool --srcdir $(shell readlink -f $(srcdir))
EXPECT = expect
RUNTEST= env --unset=DEBUGINFOD_VERBOSE --unset=DEBUGINFOD_PROGRESS XDG_DATA_DIRS= SYSTEMTAP_SYNC=1 LANG=C SYSTEMTAP_TESTREMOTES=$(TESTREMOTES) SYSTEMTAP_TESTAPPS=$(TESTAPPS) SYSTEMTAP_RUNTIME=$(SYSTEMTAP_RUNTIME) SYSTEMTAP_TAPSET=$(SYSTEMTAP_TAPSET) CRASH_LIBDIR=$(CRASH_LIBDIR) PATH=$(SYSTEMTAP_PATH):$(SYSTEMTAP_SPATH):$$PATH SYSTEMTAP_PATH=$(SYSTEMTAP_PATH) SYSTEMTAP_SPATH=$(SYSTEMTAP_SPATH) SYSTEMTAP_INCLUDES=$(SYSTEMTAP_INCLUDES) PKGLIBDIR=$(pkglibexecdir) PYTHON=$(PYTHON) PYEXECDIR=$(PYEXECDIR) PYTHON3=$(PYTHON3) PY3EXECDIR=$(PY3EXECDIR) SYSCONFDIR=$(SYSCONFDIR) runtest
