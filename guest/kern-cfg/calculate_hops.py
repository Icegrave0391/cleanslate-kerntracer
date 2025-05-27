import argparse
import networkx as nx

from tools import *
from kfunc_filter import should_filter_function


def file_to_sys2func(file_path):
    """
    Parse an unprofiled-all file into a dict mapping
    "sysid:syscall" -> [list of function names]
    """
    sys2funcs = {}
    current = None

    with open(file_path, 'r') as f:
        for line in f:
            stripped = line.strip()
            # Match lines like "0:read" or "1:write"
            m = re.match(r'^(\d+:[\w\.]+)$', stripped)
            if m:
                current = m.group(1)
                sys2funcs[current] = []
            # Match lines containing a [...] list of functions
            elif current and '[' in stripped and ']' in stripped:
                funcs_str = re.search(r'\[([^\]]*)\]', stripped).group(1)
                funcs = [fn.strip() for fn in funcs_str.split(',') if fn.strip()]
                sys2funcs[current].extend(funcs)

    # Deduplicate while preserving order
    for key, funcs in sys2funcs.items():
        seen = set()
        deduped = []
        for fn in funcs:
            if fn not in seen:
                seen.add(fn)
                deduped.append(fn)
        sys2funcs[key] = deduped

    return sys2funcs

target_syscalls = [
    "0:read",
    "1:write",
    "257:openat",
    "9:mmap",
    "11:munmap",
    "12:brk",
    "41:socket",
    "59:execve",
    "262:newfstatat",
    "45:recvfrom",
]


if __name__ == '__main__':

    parser = argparse.ArgumentParser(description="Process an unprofiled file for hop calculation.")
    parser.add_argument("-i", "--unprofiled_file_path", help="Path to the unprofiled file")
    args = parser.parse_args()

    # get unprofiled functions
    sys2funcs = file_to_sys2func(args.unprofiled_file_path)
    
    # load kernel static callgraph
    k_cg = nx.read_graphml("./callgraph.graphml").to_undirected()
    
    kall_functions = [node for node in k_cg.nodes if not should_filter_function(node)]
    
    for syscall in target_syscalls:
        profiled_functions = extract_profiled_functions(kall_functions, )
    
    nx.shortest_path_length
    import IPython; IPython.embed()
    