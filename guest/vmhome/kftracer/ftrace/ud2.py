#!/usr/bin/env python3
"""
Generate UD2 injection maps and list of executed kernel functions per syscall.

Usage:
    python generate_UD2.py <program>

This script reads the list of executed syscalls for <program> from:
    syscall_profiles/executed_syscalls/<program>.txt

For each syscall it:
 1. Loads the union of profiled kernel functions from
    syscall_profiles/<id>:<name>/<program>-all.txt
 2. Scans the kernel symbol table (file 'kallsyms') for all text symbols up to __start_rodata (end of .text).
 3. Splits symbols into 4KB pages, tracking which pages have zero profiled functions and which byte-range gaps
    within pages should be filled with UD2 instructions (unreachable code).

Outputs (written to out_UD2/<program>/):
 - whole_page_code.txt: each line N corresponds to syscall N; contains comma-separated page addresses
   where no functions were profiled, or empty if syscall N not executed.
 - ud2_sections.txt: each line N corresponds to syscall N; contains comma-separated start,end addresses
   for UD2 injection regions on each page, or empty if syscall N not executed.
 - <program>-syscall-kfuncs.txt: each line N corresponds to syscall N; contains pipe-separated kernel
   functions profiled for that syscall, or empty if syscall N not executed.
"""
import sys
import os

# Page size for x86_64
PAGE_SIZE = 0x1000

class Page:
    """
    Represents a 4KB page of kernel code.
    Tracks profiled functions within it and computes UD2 gaps.
    """
    def __init__(self, address):
        # Align address down to page boundary
        self.base = (address // PAGE_SIZE) * PAGE_SIZE
        # Holds [start1,end1,start2,end2,...]
        self.functionRange = []
        # UD2 injection ranges: [start0,end0,start1,end1,...]
        self.ud2Range = []
        # Computed later
        self.numberOfFunctions = 0

    def nextPage(self):
        return self.base + PAGE_SIZE

    def calculateNumberOfFunctions(self):
        self.numberOfFunctions = len(self.functionRange) // 2

    def reduceFunctionRange(self):
        i = 1
        while i < len(self.functionRange) - 1:
            if self.functionRange[i] == self.functionRange[i+1]:
                # Remove zero-length segments [x,x]
                self.functionRange = self.functionRange[:i] + self.functionRange[i+2:]
                i = 0
            i += 1

    def calculateUd2Range(self):
        if not self.functionRange:
            return
        # Gap at page start
        if self.functionRange[0] != self.base:
            self.ud2Range += [self.base, self.functionRange[0]]
        # Interior gaps until page end
        for addr in self.functionRange[1:]:
            if addr == self.nextPage():
                break
            self.ud2Range.append(addr)
        # Gap at page end
        if self.functionRange[-1] != self.nextPage():
            self.ud2Range.append(self.nextPage())


def usage():
    print(f"Usage: {sys.argv[0]} <program>", file=sys.stderr)
    sys.exit(1)


def main():
    # Validate arguments
    if len(sys.argv) != 2:
        usage()
    prog = sys.argv[1]

    # Paths
    base_dir = 'syscall_profiles'
    exec_file = os.path.join(base_dir, 'executed_syscalls', f"{prog}.txt")
    kallsyms_path = 'kallsyms'

    # Check inputs
    if not os.path.isfile(exec_file):
        print(f"Error: executed syscalls file '{exec_file}' not found", file=sys.stderr)
        sys.exit(1)
    if not os.path.isfile(kallsyms_path):
        print(f"Error: '{kallsyms_path}' not found", file=sys.stderr)
        sys.exit(1)

    # Dynamically locate end of kernel text via __start_rodata
    text_end = None
    with open(kallsyms_path) as ks:
        for line in ks:
            parts = line.split()
            if len(parts) >= 3 and parts[2] == '__start_rodata':
                text_end = int(parts[0], 16)
                break
    if text_end is None:
        print("Error: __start_rodata not found in kallsyms", file=sys.stderr)
        sys.exit(1)

    # Load executed syscalls: id -> name, track max ID
    exec_sys = {}
    max_id = -1
    with open(exec_file) as ef:
        for line in ef:
            tok = line.strip()
            if not tok:
                continue
            sid_str, sname = tok.split(':', 1)
            sid = int(sid_str)
            exec_sys[sid] = sname
            max_id = max(max_id, sid)

    # Prepare output arrays aligned by syscall ID
    code_lines = [''] * (max_id + 1)
    ud2_lines  = [''] * (max_id + 1)
    kfuncs_lines = [''] * (max_id + 1)

    # Process each syscall
    for sid in sorted(exec_sys):
        sname = exec_sys[sid]
        # Read profiled func list
        fn_file = os.path.join(base_dir, f"{sid}:{sname}", f"{prog}-all.txt")
        if not os.path.isfile(fn_file):
            print(f"Warning: '{fn_file}' missing, skipping syscall {sid}", file=sys.stderr)
            continue
        profiled = set(line.strip() for line in open(fn_file) if line.strip())

        # Record profiled functions line
        kfuncs_lines[sid] = '|'.join(sorted(profiled))

        # Walk kallsyms to build pages
        pages = []
        page = Page(0)
        with open(kallsyms_path) as ks:
            for kl in ks:
                parts = kl.split()
                if len(parts) < 3:
                    continue
                addr = int(parts[0], 16)
                sym_type = parts[1]
                fn_name  = parts[2]
                # Skip non-text or beyond .text region
                if sym_type not in ('T','t') or addr >= text_end:
                    continue
                # Page boundary crossed?
                while addr >= page.nextPage():
                    page.calculateNumberOfFunctions()
                    page.reduceFunctionRange()
                    page.calculateUd2Range()
                    pages.append(page)
                    page = Page(addr)
                # If profiled, record
                if fn_name in profiled:
                    if not page.functionRange or page.functionRange[-1] != addr:
                        page.functionRange.append(addr)
                        page.functionRange.append(min(addr, page.nextPage()))
            # Finalize last page
            page.calculateNumberOfFunctions()
            page.reduceFunctionRange()
            page.calculateUd2Range()
            pages.append(page)

        # Drop dummy first page
        pages = pages[1:]

        # Pages with zero functions → full-page UD2
        code_addrs = [hex(p.base) for p in pages if p.numberOfFunctions == 0]
        code_lines[sid] = ','.join(code_addrs)

        # Gather UD2 ranges
        ud2_addrs = []
        for p in pages:
            ud2_addrs.extend(p.ud2Range)
        ud2_lines[sid] = ','.join(hex(x) for x in ud2_addrs)

    # Prepare output directory
    out_dir = os.path.join('out_UD2', prog)
    os.makedirs(out_dir, exist_ok=True)

    # Write whole_page_code.txt
    with open(os.path.join(out_dir, 'whole_page_code.txt'), 'w') as outf:
        for line in code_lines:
            outf.write(line + '\n')
    # Write ud2_sections.txt
    with open(os.path.join(out_dir, 'ud2_sections.txt'), 'w') as outf:
        for line in ud2_lines:
            outf.write(line + '\n')
    # Write <program>-syscall-kfuncs.txt
    with open(os.path.join(out_dir, f"{prog}-syscall-kfuncs.txt"), 'w') as outf:
        for line in kfuncs_lines:
            outf.write(line + '\n')

    print(f"Generated outputs in {out_dir}")


if __name__ == '__main__':
    main()
