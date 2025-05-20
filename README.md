# cleanslate-kerntracer

## Workflow

**1. Enter the Guest VM**

```bash
$ cd guest
$ sudo ./start-vm.sh
```

**2. Profile**

For a server-side background program:

```bash
$ cd kftracer/ftrace
# Trace the server in the VM
$ ./trace-server.sh <server-prog-name> <output-raw-data.dat>
# Once finishing workload execution from the client-side
$ Ctrl^C 
```

For a command/foreground program (TBD).

**3. Trace recovery/parse**

```bash
$ cd kftracer/ftrace
# recover per-thread (CPU) function traces
$ ./report.sh <raw-data.dat> <prog-name>
# parse the per-thread traces
$ python3 ./syscall-kfunc-parser.py <prog-name> <file-id>
```

After this, all results will be generated at `kftracer/ftrace/syscall_profiles/.../<prog-name>-<file-id>.txt`.

- Per-syscall functions will be generated at: `syscall_profiles/<sys_id>:<sys_name>/<prog-name>-<file-id>.txt`.
- Common functions will be merged at: `syscall_profiles/common/<prog-name>.txt`.
- All executed syscalls (by the program) will be merged at: `syscall_profiles/executed_syscalls/<prog-name>.txt`.

**4. Persistence**

Even after shutting down the guest VM, all results/scripts will be synchronized to the host filesystem.

All guest data will be synced to `guest/vmhome/`.
