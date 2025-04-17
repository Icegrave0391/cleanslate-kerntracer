#ifndef TASK_FINDER_C
#define TASK_FINDER_C

/*
 * Which utrace shall we use?
 * (1) Built-in kernel utrace (preferred)
 * (2) Internal utrace.  Requires STAPCONF_UTRACE_VIA_TRACEPOINTS.
 * (3) If we don't have either (old kernels or new kernels without all
 * the pre-requisites), error.
 */

#ifndef HAVE_TASK_FINDER
#error "Process probes not available without kernel CONFIG_TRACEPOINTS/CONFIG_ARCH_SUPPORTS_UPROBES/CONFIG_UPROBES. The latter method also requires specific tracepoints and task_work_add()."
#else  /* HAVE_TASK_FINDER */
#include "task_finder2.c"
#endif /* HAVE_TASK_FINDER */
#endif /* TASK_FINDER_C */
