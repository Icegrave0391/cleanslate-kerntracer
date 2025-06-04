import argparse
import os
import re
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.font_manager as fm

from tools import *
from kfunc_filter import should_filter_function

# 如果你有 Times New Roman 字体文件，把它路径填到这里
font_path = './times-new-roman.ttf'
fm.fontManager.addfont(font_path)
plt.rcParams['font.family'] = 'Times New Roman'
plt.rcParams.update({'font.size': 20})

def read_percentage(file_path):
    """
    Read the first line of file_path and extract the percentage of [YES].
    Expected format: "Percentage of [YES]: 86.00%"
    Returns a float.
    """
    with open(file_path, 'r') as f:
        first_line = f.readline().strip()
    m = re.search(r'Percentage of \[YES\]:\s*([\d\.]+)%', first_line)
    if not m:
        raise ValueError(f"Cannot parse percentage from {file_path}: '{first_line}'")
    return float(m.group(1))

if __name__ == "__main__":
    target_syscalls = [
        "0:read",
        "1:write",
        "3:close",
        "257:openat",
        "9:mmap",
        "11:munmap",
        "12:brk",
        "41:socket",
        "59:execve",
        "262:newfstatat",
    ]

    base_dir = "syscall_procs"

    names = []
    vals = []
    vals_noctx = []

    for sysid_name in target_syscalls:
        # Extract syscall name (part after ':')
        name = sysid_name.split(":", 1)[1]
        names.append(name)

        dir_path = os.path.join(base_dir, sysid_name)
        val_file = os.path.join(dir_path, "validation.txt")
        noctx_file = os.path.join(dir_path, "validation_noctx.txt")

        # Read percentages
        pct_val = read_percentage(val_file)
        pct_noctx = read_percentage(noctx_file)

        vals.append(pct_val)
        vals_noctx.append(pct_noctx)

    # Convert to numpy arrays for plotting
    vals = np.array(vals)
    vals_noctx = np.array(vals_noctx)
    x = np.arange(len(names))


    # Compute and print mean accuracies
    mean_vals = vals.mean()
    mean_noctx = vals_noctx.mean()
    print(f"Mean accuracy (LLM-RAG): {mean_vals:.2f}%")
    print(f"Mean accuracy (LLM-nonRAG): {mean_noctx:.2f}%")

    width = 0.25  # width of the bars

    fig, ax = plt.subplots(figsize=(9, 4))
    bars1 = ax.bar(x - width/2, vals, width, label='LLM-RAG',color='#4C72B0')
    bars2 = ax.bar(x + width/2, vals_noctx, width, label='LLM-nonRAG',color='#55A868')

    # Add labels, title, and legend
    ax.set_xticks(x)
    ax.set_xticklabels(names, rotation=30, ha='right')

    ax.set_ylabel('Prediction accuracy (%)')
    ax.set_yticks([0, 25, 50, 75, 100])
    ax.legend(ncols=2)

    plt.tight_layout()
    # plt.show()
    plt.savefig("llm_validation.pdf", bbox_inches='tight')
