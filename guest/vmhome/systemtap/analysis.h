// -*- C++ -*-
// Copyright (C) 2021 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#ifndef ANALYSIS_H
#define ANALYSIS_H
#include "config.h"

// Three outcomes of analysis:
// <  0 false
// == 0 unable to determine
// >  0 true

#ifdef HAVE_DYNINST

extern int liveness(systemtap_session& s,
		    target_symbol *e,
		    std::string executable,
		    Dwarf_Addr location,
		    location_context ctx);

extern void flush_analysis_caches();
#else

#define liveness(session, target, executable, location, var) (0)
#define flush_analysis_caches() {/* nothing to do */}

#endif // HAVE_DYNINST
#endif // ANALYSIS_H

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
