// systemtap analysis code
// Copyright (C) 2021 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#include "config.h"
#include "session.h"

#ifdef HAVE_DYNINST

#include "loc2stap.h"
#include "analysis.h"
#include <dyninst/Symtab.h>
#include <dyninst/Function.h>
#include <dyninst/liveness.h>
#include <dyninst/dyn_regs.h>

using namespace Dyninst;
using namespace SymtabAPI;
using namespace ParseAPI;
using namespace std;


// Data structures to cache dyninst parsing of binaries
class bin_info {
public:
  bin_info(SymtabCodeSource *s=NULL, CodeObject *c=NULL, SymtabAPI::Symtab *sym=NULL): symtab(sym), sts(s), co(c) {};
	~bin_info(){};
	SymtabAPI::Symtab *symtab;
	SymtabCodeSource *sts;
	CodeObject *co;
};
typedef map<string, bin_info> parsed_bin;
static parsed_bin cached_info;

// Clean things up when analysis no longer needs the cached dyninst objects
void flush_analysis_caches()
{
  for(auto i: cached_info) {
    delete i.second.co;
    delete i.second.sts;
    SymtabAPI::Symtab::closeSymtab(i.second.symtab);
  }
  cached_info.clear();
}


class analysis {
public:
	analysis(string name);
	SymtabCodeSource *sts;
	CodeObject *co;
};

//  Get the binary set up for anaysis
analysis::analysis(string name)
{
	char *name_str = strdup(name.c_str());
	sts = NULL;
	co = NULL;
	SymtabAPI::Symtab *symTab;
	bool isParsable;

	// Use cached information if available
	if (cached_info.find(name) != cached_info.end()) {
		sts = cached_info[name].sts;
		co = cached_info[name].co;
		goto cleanup;
	}

	// Not not seen before
	// Create a new binary code object from the filename argument
	isParsable = SymtabAPI::Symtab::openFile(symTab, name_str);
	if(!isParsable) goto cleanup;

	sts = new SymtabCodeSource(symTab);
	if(!sts) goto cleanup;

	co = new CodeObject(sts);
	if(!co) goto cleanup;

	// Cache the info for future reference
	{
		bin_info entry(sts,co,symTab);
		cached_info.insert(make_pair(name,entry));
	}

cleanup:
	free(name_str);
}

#if defined(__i386__) || defined(__x86_64__)
static const MachRegister dyninst_register_64[] = {
	x86_64::rax,
	x86_64::rdx,
	x86_64::rcx,
	x86_64::rbx,
	x86_64::rsi,
	x86_64::rdi,
	x86_64::rbp,
	x86_64::rsp,
	x86_64::r8,
	x86_64::r9,
	x86_64::r10,
	x86_64::r11,
	x86_64::r12,
	x86_64::r13,
	x86_64::r14,
	x86_64::r15,
	x86_64::rip
};

static const MachRegister dyninst_register_32[] = {
	x86::eax,
	x86::edx,
	x86::ecx,
	x86::ebx,
	x86::esi,
	x86::edi,
	x86::ebp,
	x86::esp
};

#elif defined(__aarch64__)
static const MachRegister dyninst_register_64[] = {
	aarch64::x0,
	aarch64::x1,
	aarch64::x2,
	aarch64::x3,
	aarch64::x4,
	aarch64::x5,
	aarch64::x6,
	aarch64::x7,
	aarch64::x8,
	aarch64::x9,
	aarch64::x10,
	aarch64::x11,
	aarch64::x12,
	aarch64::x13,
	aarch64::x14,
	aarch64::x15,
	aarch64::x16,
	aarch64::x17,
	aarch64::x18,
	aarch64::x19,
	aarch64::x20,
	aarch64::x21,
	aarch64::x22,
	aarch64::x23,
	aarch64::x24,
	aarch64::x25,
	aarch64::x26,
	aarch64::x27,
	aarch64::x28,
	aarch64::x29,
	aarch64::x30,
	aarch64::sp
};

static const MachRegister dyninst_register_32[1]; // No 32-bit support

#elif defined(__powerpc__)
/* For ppc64 still use the ppc32 register names */
static const MachRegister dyninst_register_64[] = {
    ppc32::r0,
    ppc32::r1,
    ppc32::r2,
    ppc32::r3,
    ppc32::r4,
    ppc32::r5,
    ppc32::r6,
    ppc32::r7,
    ppc32::r8,
    ppc32::r9,
    ppc32::r10,
    ppc32::r11,
    ppc32::r12,
    ppc32::r13,
    ppc32::r14,
    ppc32::r15,
    ppc32::r16,
    ppc32::r17,
    ppc32::r18,
    ppc32::r19,
    ppc32::r20,
    ppc32::r21,
    ppc32::r22,
    ppc32::r23,
    ppc32::r24,
    ppc32::r25,
    ppc32::r26,
    ppc32::r27,
    ppc32::r28,
    ppc32::r29,
    ppc32::r30,
    ppc32::r31
};

static const MachRegister dyninst_register_32[] = {
    ppc32::r0,
    ppc32::r1,
    ppc32::r2,
    ppc32::r3,
    ppc32::r4,
    ppc32::r5,
    ppc32::r6,
    ppc32::r7,
    ppc32::r8,
    ppc32::r9,
    ppc32::r10,
    ppc32::r11,
    ppc32::r12,
    ppc32::r13,
    ppc32::r14,
    ppc32::r15,
    ppc32::r16,
    ppc32::r17,
    ppc32::r18,
    ppc32::r19,
    ppc32::r20,
    ppc32::r21,
    ppc32::r22,
    ppc32::r23,
    ppc32::r24,
    ppc32::r25,
    ppc32::r26,
    ppc32::r27,
    ppc32::r28,
    ppc32::r29,
    ppc32::r30,
    ppc32::r31
};
#endif

// Data structures to cache dyninst liveness analysis of a function
typedef map<string, LivenessAnalyzer*> precomputed_liveness;
static precomputed_liveness cached_liveness;

int liveness(systemtap_session& s,
	     target_symbol *e,
	     string executable,
	     Dwarf_Addr addr,
	     location_context ctx)
{
  try{
	// Doing this inside a try/catch because dyninst may require
	// too much memory to parse the binary.
	// should cache the executable names like the other things
	analysis func_to_analyze(executable);
	MachRegister r;

	// Punt if unsuccessful in parsing binary
	if (!func_to_analyze.co){
		s.print_warning(_F("liveness analysis unable to parse binary %s",
				   executable.c_str()), e->tok);
		return 0;
	}

	// Determine whether 32-bit or 64-bit code as the register names are different in dyninst
	int reg_width = func_to_analyze.co->cs()->getAddressWidth();

	// Find where the variable is located
	location *loc = ctx.locations.back ();

	// If variable isn't in a register, punt (return 0)
	if (loc->type != loc_register) return 0;

	// Map dwarf number to dyninst register name, punt if out of range
	unsigned int regno = loc->regno;
	switch (reg_width){
	case 4:
		if (regno >= (sizeof(dyninst_register_32)/sizeof(MachRegister))) return 0;
		r = dyninst_register_32[regno]; break;
	case 8:
		if (regno >= (sizeof(dyninst_register_64)/sizeof(MachRegister))) return 0;
		r = dyninst_register_64[regno]; break;
	default:
		// All the current architectures that systemtap (and dyninst) support
		// are 32-bit (4 byte) or 64-bit (8 byte). Should never end up here.
		assert(false);
		return 0;
	}

	// Find the function containing the probe point.
	std::set<ParseAPI::Function*> ff_s;
	if(func_to_analyze.co->findFuncs(NULL, addr, ff_s) <= 0) return 0;
	ParseAPI::Function *func = *ff_s.begin();

	LivenessAnalyzer *la;
	// LivenessAnalyzer does allow some caching on a per executable basis
	// Check if a previous liveness analyzer exists for the executable
	if (cached_liveness.find(executable) != cached_liveness.end()) {
		la = cached_liveness[executable];
	}else {
		// Otherwise create new liveness analysis
		la = new LivenessAnalyzer(reg_width);
		cached_liveness.insert(make_pair(executable,la));
	}
	la->analyze(func);

	// Get the basic block and instruction containing the the probe point.
	set<Block *> bb_s;
	if (func_to_analyze.co->findBlocks(NULL, addr, bb_s) != 1 )
		return 0; // too many (or too few) basic blocks, punt
	Block *bb = *bb_s.begin();

	// Construct a liveness query location for the probe point.
	InsnLoc i(bb,  addr, bb->getInsn(addr));
	Location iloc(func, i);

	// Query to see if whether the register is live at that point
	bool used;
	la->query(iloc, LivenessAnalyzer::Before, r, used);
	return (used ? 1 : -1);
  } catch (std::bad_alloc & ex){
    s.print_warning(_F("unable to allocate memory for liveness analysis of %s",
				   executable.c_str()), e->tok);
    return 0;
  }
}

#endif // HAVE_DYNINST
