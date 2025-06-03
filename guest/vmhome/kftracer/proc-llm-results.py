import os
import re

base_dir = "syscall_procs"

res_nothink = "result_nothink.txt"
res_think = "result_think.txt"

out_nothink = "result_nothink"
out_think = "result_think"

def proc_llm_file(file_path):
    func_set = set()
    with open(file_path, 'r') as f:
        for line in f:
            # Process each line in the LLM result file
            stripped = line.strip()
            if stripped.startswith('- '):
                func_name = stripped[2:]
                func_name = re.sub(r'<profiled>', '', func_name).strip()
                func_set.add(func_name)
    print(f"Processed {len(func_set)} functions from {file_path}")
    return func_set

def proc_path(dir, full_path):
    if "result_nothink.txt" in str(full_path):
        out_path =  os.path.join(dir, out_nothink)
    elif "result_think.txt" in str(full_path):
        out_path =  os.path.join(dir, out_think)
    else:
        raise ValueError(f"Unexpected file in {full_path}")

    func_set = proc_llm_file(full_path)
    with open(out_path, 'w') as out_file:
        for func in sorted(func_set):
            out_file.write(f"{func}\n")
    

for root, dirs, files in os.walk(base_dir):
    for dir_name in dirs:
        subdir_path = os.path.join(root, dir_name)
        print(f"Subdirectory: {subdir_path}")
        
        think_path = os.path.join(subdir_path, res_think)
        if os.path.isfile(think_path):
            proc_path(subdir_path, think_path)

        nothink_path = os.path.join(subdir_path, res_nothink)
        if os.path.isfile(nothink_path):
            proc_path(subdir_path, nothink_path)