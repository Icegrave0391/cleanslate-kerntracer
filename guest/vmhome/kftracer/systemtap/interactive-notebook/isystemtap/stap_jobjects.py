import subprocess
from queue import Queue
from threading import Thread
import os
import time
import json5
import re
import sys
import ast
from bqplot import *
import bqplot.pyplot as plt
import sqlite3
from typing import List
from .stap_kernel import *
from ipyevents import Event
from ipywidgets import Text, Button, Tab, GridBox, Layout, Label, HBox, VBox, Output, TagsInput, Combobox, IntProgress, HTML, Valid
from IPython.display import JSON, Code, Markdown, Javascript
from shlex import quote
# FIXME: poll's small changes should be added to the jupyter-ui-poll, and the file should be removed
from .poll import ui_events

JUPYTER_MAGIC = "JUPYTER_MAGIC_"
# File is created at build time
try:
    from .constants import STAP_PKGDATADIR, STAP_PREFIX
    STAP_INSTALL_DIR = f'{STAP_PREFIX}/bin'
    SUBPROCESS_ENV   = {**os.environ, 'PATH':f'{STAP_INSTALL_DIR}:{os.environ["PATH"]}'}
except:
    # An educated guess
    STAP_PKGDATADIR   = '/usr/local/share/systemtap'
    STAP_INSTALL_DIR = '/usr/bin'
    SUBPROCESS_ENV   = None

# When running in a container we ssh back to the host to run stap. The dest is the host ip we use (localhost here)
try:
    from .constants import SSH_DEST
except:
    SSH_DEST = None

def format_subprocess_args(*args):
    flattened_args = []
    for a in args:
        if type(a) is list:
            flattened_args.extend(a)
        else:
            flattened_args.append(a)
    
    if SSH_DEST:
        command =   f'PATH={STAP_INSTALL_DIR+":$PATH"} ' + " ".join([quote(a) for a in flattened_args])
        flattened_args = ['ssh', SSH_DEST, command]

    return flattened_args


class JSubprocess(subprocess.Popen):
    """
    A subprocess that allows to read its stdout and stderr in real time
    Based on https://github.com/brendan-rius/jupyter-c-kernel/blob/master/jupyter_c_kernel/kernel.py
    """

    def __init__(self, cmd):
        super().__init__(cmd, stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=0)

        self._stdout_queue = Queue()
        self._stdout_leftovers = list()
        self._stdout_thread = Thread(
            target=JSubprocess._enqueue_output, args=(self.stdout, self._stdout_queue))
        self._stdout_thread.daemon = True
        self._stdout_thread.start()

        self._stderr_queue = Queue()
        self._stderr_leftovers = list()
        self._stderr_thread = Thread(
            target=JSubprocess._enqueue_output, args=(self.stderr, self._stderr_queue))
        self._stderr_thread.daemon = True
        self._stderr_thread.start()

    def write_to_stdin(self, contents):
        self.stdin.write(contents.encode())
        self.stdin.flush()

    @staticmethod
    def _enqueue_output(stream, queue):
        """
        Add chunks of data from a stream to a queue until the stream is empty.
        """
        for line in iter(lambda: stream.read(4096), b''):
            queue.put(line)
        stream.close()

    def get_lines(self, retain_partial_lines=True):
        """
        Return a list of tuples (line, stream).
        Unless retain_partial_lines is False
        it is safe to assume a line is complete (ends with \n)
        """
        lines = []
        def read_all_from_queue(queue, leftovers, stream_name):
            res = b''.join(leftovers)
            leftovers.clear()
            size = queue.qsize()
            while size != 0:
                res += queue.get_nowait()
                size -= 1
            raw_lines = res.split(b'\n')
            if len(res) > 0 and res[-1] != b'\n' and retain_partial_lines:
                leftovers.append(raw_lines.pop())
            lines.extend([(line + b'\n', stream_name)
                         for line in raw_lines if line != b''])

        read_all_from_queue(self._stdout_queue,
                            self._stdout_leftovers, "stdout")
        read_all_from_queue(self._stderr_queue,
                            self._stderr_leftovers, "stderr")
        return lines


class JHistogram():
    def __init__(self, kernel, func, name, start=0, end=0, width=0):
        self.kernel = kernel
        self.name = name
        self.func = func
        self.start = int(start)
        self.end = int(end)
        self.width = int(width)
        self.data = []

    def record_entry(self, bucket, value):
        self.data.append((bucket, value))

    def display_hist(self):
        bins   = [p[0] for p in self.data]
        counts = [p[1] for p in self.data]
        try:
            # bqplot bar plot
            scale_x = OrdinalScale()
            scale_y = LinearScale()
            fig = plt.figure(
                title=f'{"Linear" if self.func == "linear" else "Logarithmic"} Histogram of {self.name}')
            def_tt = Tooltip(fields=['x', 'y'], labels=['Value', 'Count'])
            bar = plt.bar(x=bins, y=counts,
                          scales={'x': scale_x, "y": scale_y},
                          colors=['coral'], stroke='white', stroke_width=0.4,
                          tooltip=def_tt)
            self.kernel.display(fig)
        except Exception as e:
            self.kernel.error(e)


class JNamespace:
    def __init__(self, kernel, name=""):
        self.name    : str             = name
        # If "name" is used as a module name will be updated to "name"i at runtime
        self.mod_name: str             = name
        self.kernel                    = kernel
        self._cells  : List[JCell]     = list()
        self._proc   : JSubprocess     = None
        self._globals: dict            = dict()

    def __del__(self):
        # If the destructor is called, remove the namespace
        # from the kernel's map
        self.kernel.jnamespaces.pop(self.name, None)

    def add_cell(self, cell):
        self._cells.append(cell)

    def remove_cell(self, cell):
        if cell in self._cells:
            self._cells.remove(cell)

    def remove_cell_by_id(self, cell_id):
        self._cells = [c for c in self._cells if c.cell_id != cell_id]

    def remove_all_cells(self):
        self._cells.clear()

    def get_cells(self):
        return self._cells

    def update_globals(self, globals_dict):
        """Update the internal states of the namespace's globals"""
        # Keep what is already stored
        if globals_dict is None:
            return
        globs = {}
        for g, v in globals_dict.items():
            if isinstance(v, dict):
                # An array so parse the keys
                # May have tuple keys for stap assosiative array
                # For stats keys will just be unparsed strings
                globs[g] = {(ast.literal_eval(
                    k) if "\"" in k or "(" in k else k): vi for k, vi in v.items()}
            else:
                globs[g] = v
        self._globals = globs

    def get_globals(self):
        return self._globals


class CellExeMode():
    EDIT = "edit"
    RUN = "run"
    SCRIPT = "script"
    PYTHON = "python"
    EXAMPLES = "examples"
    PROBES = "probes"
    HELP = "help"
    UNDEF = "undefined"


class JCell:
    def __init__(self, cell_id: str, kernel):
        self.cell_id               = cell_id
        self.kernel                = kernel
        self.namespace: JNamespace = None
        self.mode                  = CellExeMode.UNDEF
        self.script                = ""
        self.options               = list()
        self.args                  = list()
        self.hist: JHistogram      = None

    def __del__(self):
        # When a cell's dtor is called, make sure to remove it
        # (not just a cell with the same id) from the namespace too
        if self.namespace:
            self.namespace.remove_cell(self)
            if self.namespace.get_cells == []:
                del self.namespace

    def _load_script(self, code: str):
        script = ""
        namespace = ""
        try:
            for line in code.split("\n"):
                # We found a possible magic header
                if len(line) >= 2 and line[:2] == "%%":
                    if self.mode != CellExeMode.UNDEF:
                        self.kernel.error(
                            "Only 1 magic command is allowed per cell")
                        return False

                    # Before anything else, see if our internal special argument (--args)
                    # exists, and if so extract and parse it
                    a_start = line.find('--args')
                    a_open, a_close = line.find(
                        '(', a_start), line.rfind(')', a_start)
                    if a_start != -1:
                        if a_open > a_close or a_open == -1 or a_close == -1:
                            self.kernel.error(
                                "Invalid args usage: Should be --args=(x,y,z)")
                            return False
                        else:
                            self.args = line[a_open+1: a_close].split(",")
                            # We've removed the args, so the line is updated accordingly
                            line = line[:a_start] + line[a_close+1:]

                    split_magic = line[2:].rstrip().split(" ")
                    command = split_magic.pop(0)

                    # Cells with namespaces
                    if command in (CellExeMode.EDIT, CellExeMode.RUN, CellExeMode.PYTHON, CellExeMode.SCRIPT):
                        # We have a magic with a namespace provided
                        # otherwise namespace = "" i.e the global namespace
                        if len(split_magic) >= 1 and split_magic[0][0] != '-':
                            namespace = split_magic.pop(0)

                    # Cells with arguments
                    if command != CellExeMode.SCRIPT and self.args != []:
                        self.kernel.error(
                            "Invalid args usage: Should only be used with script cells")
                        return False
                    # Cells with options
                    VALID_OPTIONS = ('-v', '--vp', '-V', '--version', '-P', '-u', '-w', '-W', '-t', '-s', '-I', '-D',
                                     '-B', '-a', '--modinfo', '-r', '-d', '--ldd', '--all-modules', '-c', '-x', '-T', '-g',
                                     '--prologue-searching', '--suppress-handler-errors', '--compatible', '--check-version',
                                     '--disable-cache', '--poison-cache', '--privilege', '--unprivileged', '--download-debuginfo',
                                     '--rlimit-', '--sysroot', '--runtime', '--dyninst', '--bpf', '--suppress-time-limits')
                    if command not in (CellExeMode.EDIT, CellExeMode.RUN, CellExeMode.SCRIPT) and split_magic != []:
                        self.kernel.error(
                            "Invalid options usage: Should only be used with run, edit and script cells. Skipping line")
                        return False
                    else:
                        self.options = split_magic
                        # Make sure the flags are all valid
                        if any(all(not flag.startswith(valid_flag) for valid_flag in VALID_OPTIONS) for flag in [o for o in self.options if o[0] == '-']):
                            self.kernel.error(
                                f'Invalid Options: {self.options}. Only {VALID_OPTIONS} can be used')
                            return False

                    if command not in (CellExeMode.EDIT, CellExeMode.EXAMPLES, CellExeMode.HELP,
                                       CellExeMode.PROBES, CellExeMode.PYTHON, CellExeMode.RUN, CellExeMode.SCRIPT):
                        self.kernel.error(
                            f"Skipping unknown magic command {command}")
                        return False
                    self.mode = command
                elif len(line) >= 1 and line[0] == "!":
                    p = subprocess.run(format_subprocess_args(line[1:].split(" ")), capture_output=True, text=True, env=SUBPROCESS_ENV)
                    if p.stdout:
                        self.kernel.write(p.stdout)
                    if p.returncode != 0:
                        self.kernel.error(p.stderr)
                        return False
                else:
                    # We want to find lines which print histograms and bookend them with magic commands that isystemtap
                    # will recognize to replace the output with our own.
                    r = re.compile(
                        r'(.*?(?=print))(print.*\(.*\@hist_)(.*\)*)(.*)')
                    m = r.search(line)
                    if m:
                        prefix   = m[1]  # Stuff before
                        operator = m[2]  # Ex. 'print(@hist_'
                        operands = m[3]  # Ex. 'log(foo)' or 'linear(a, 1,2, 3)'
                        suffix   = m[4]  # Stuff after
                        self.kernel.log.debug(
                            f'Parsed histogram with {prefix, operator, operands, suffix = }')
                        # Don't magic up commented out histograms
                        if any(c in prefix for c in ('//', '#', '/*')):
                            self.kernel.log.debug(
                                'Skipping commented histogram')
                            script += line+"\n"
                            continue
                        # We can't print a histogram for '@hist_log(foo)[0] or @hist_linear(bar[baz], 1, 2 , 3)[2]'
                        # If a ) comes before a [ then we can say that the hist_foo(...) is done and then an index is started so skip
                        if operands.find(")") < operands.rfind("["):
                            self.kernel.log.debug(
                                'Skipping hisogram which is being indexed')
                            script += line+"\n"
                            continue
                        # The inner most '(' and the outmost ')'
                        paren_open, paren_close = operands.rfind(
                            "("), operands.find(')')
                        # We use the commands hist_FUNC:ARG1:... and hist_end
                        args = [operands[:paren_open]] + [s.strip()
                                                          for s in operands[paren_open + 1:paren_close].split(",")]

                        # We want to display the literal for scalar stats BUT the value for the arguments to arrays
                        # Ex. hist_log(foo[name]) should be called foo[VALUE_OF_NAME]
                        p1_args = ""
                        if '[' in args[1]:
                            index_open  = args[1].find("[")
                            index_close = args[1].find(']')
                            subscript = args[1][index_open+1: index_close]
                            p1_args = f', {subscript}'
                            # Replace the subscript with the appropriate %s, ...
                            args[1] = args[1][:index_open+1] + ", ".join(
                                ["%s" for _ in subscript.split(",")]) + args[1][index_close:]
                        p1 = "hist_" + ":".join(args)
                        p2 = "hist_end"
                        line = prefix + f' ; printf("\\n{JUPYTER_MAGIC}{p1}\\n"{p1_args}); ' + \
                            operator + operands + \
                            f' ; println("\\n{JUPYTER_MAGIC}{p2}\\n"); ' + suffix
                        self.kernel.log.debug(
                            f'Histogram line replaced with { line }')

                    script += line+"\n"
        except:
            return False

        if self.mode == CellExeMode.UNDEF and script != "":
            self.kernel.print(
                "Defaulting to a script cell")
            self.mode = CellExeMode.SCRIPT

        if not re.match(r"^[a-z0-9_]*$", namespace):
            self.kernel.error(
                f'"{namespace}" is not a valid namespace i.e ^[a-z0-9_]*$')
            return False
        
        if namespace == "":
            namespace = "isystemtap_global"

        # We get a namespace BUT are not yet added to it, that's only done once validity is checked
        if namespace not in self.kernel.jnamespaces:
            ns = JNamespace(self.kernel, namespace)
            self.namespace = ns
            self.kernel.jnamespaces[namespace] = ns
        else:
            self.namespace = self.kernel.jnamespaces[namespace]
        # FIXME: What if the cell was in a different namespace
        self.namespace.remove_cell_by_id(self.cell_id)
        self.script = script
        self.kernel.log.debug(
            f'Loaded cell of type {self.mode} into namespace "{self.namespace.name}" with args {self.args} and options {self.options}')
        self.kernel.log.debug(f'The script is: {self.script}')
        return True

    def _validate_syntax(self):
        """
        Run pass 1; To quickly detect any syntax errors
        """
        p = subprocess.run(format_subprocess_args("stap", "-e", self.script,
                           "-p1"), capture_output=True, text=True, env=SUBPROCESS_ENV)
        if p.returncode == 0:
            return True
        else:
            self.kernel.send_response(self.kernel.iopub_socket, 'stream', {
                                      'name': "stderr", 'text': p.stderr})
            return False

    def _validate_symbols_and_types(self):
        """
        Run pass 2 against all of the cells within the namespace, combining them into 1 large virtual_script (In the order they are executed)
        On success add this cell to the namespace's execution queue otherwise remove any info from this cell, since its invalid
        """

        # Combine the various partial-scripts
        virtual_script = "".join(cell.script for cell in self.namespace.get_cells())
        virtual_script += self.script

        # In order to avoid a premature no probes found error just add a never probe
        # This will never make it to stap passes 3+, just temporarily avoid the error
        virtual_script += '\n probe never { printf("Spam, Ham and Eggs") } '

        p = subprocess.run(format_subprocess_args("stap", self.options, "-e", virtual_script,
                           "-p2"), capture_output=True, text=True, env=SUBPROCESS_ENV)
        self.kernel.log.debug(f'Executing {p.args}')
        if p.returncode == 0:
            self.namespace.add_cell(self)
            return True
        else:
            self.kernel.send_response(self.kernel.iopub_socket, 'stream', {
                                      'name': "stderr", 'text': p.stderr})
            return False

    def _run_namespace(self):
        # Stitch together the cells into 1 virtual script
        virtual_script = "".join(cell.script for cell in self.namespace.get_cells())

        # We need to ensure the module name is not in use already
        self.namespace.mod_name = self.namespace.name
        i = 1
        with open("/proc/modules", "r") as f:
            # Technically this introduces a race condition where the modules list is only loaded once
            # so k users could simultaneously find the same 'free' mod name. But even if restricted to a read each
            # loop iter the race condition remains, so just leave it since this is faster.
            modules = f.read()
            while True:
                if self.namespace.mod_name not in modules:
                    break
                self.namespace.mod_name = f'{self.namespace.name}{i}'
                i += 1

        extra_options = [
            "-m", self.namespace.mod_name,
            "--monitor", #FIXME: Is there a way to wait for --monitor mode to start up fully
            "-D", "MAXSTRINGLEN=4096",
            "--skip-badvars",
            "-e", virtual_script
        ]
        cmd = format_subprocess_args("stap", self.options, extra_options, self.args)
        self.kernel.log.info(f'Executing {cmd}')
        p = JSubprocess(cmd)

        proc_path = f'/proc/systemtap/{self.namespace.mod_name}'
        def monitor_write(content):
            try:
                f = open(proc_path+"/monitor_control", 'w')
                f.write(content)
                f.flush()
                f.close()
            except OSError:
                pass

        # Tab 0: Controls
        class tab_controls:
            def __init__(self, namespace, options, args):
                self.uptime = Label(value="Uptime: 00:00:00")
                self.memory = Label(
                    value="Memory: 0data/0text/0ctx/0net/0alloc KB")

                self.play_pause = Button(
                    button_style='info', tooltip='Pause/resume script by toggling off/on all probes', icon='pause')
                def handle_play_pause(button):
                    if button.icon == "pause":  # Script is running
                        monitor_write('pause')
                        button.icon = "play"
                    else:
                        monitor_write('resume')
                        button.icon = "pause"
                self.play_pause.on_click(handle_play_pause)

                self.stop = Button(button_style='danger',
                                   tooltip='Quit script', icon='stop')
                self.stop.on_click(lambda _: monitor_write('quit'))

                self.reset = Button(
                    button_style='info', tooltip='Reset all global variables to their initial states', icon='rotate-left')
                self.reset.on_click(lambda _: monitor_write('clear'))

                self.export = Button(
                    button_style='info', tooltip='Export the script', icon='download')
                self.export_out = Output()
                self.export_hbox = HBox([self.export,self.export_out])
                def handle_export(btn):
                    btn.icon='check-circle'
                    btn.disabled = True

                    file_content =  f'#!stap {" ".join(options)} {" ".join(args)}\n'
                    file_content += virtual_script

                    # The following is based on
                    # https://stackoverflow.com/questions/3749231/download-file-using-javascript-jquery
                    jscode = '                                            \
                    const blob = new Blob([%s], { type : "plain/text" }); \
                    const url = window.URL.createObjectURL(blob);         \
                    const a = document.createElement("a");                \
                    a.style.display = "none";                             \
                    a.href = url;                                         \
                    a.download = "%s";                                    \
                    document.body.appendChild(a);                         \
                    a.click();                                            \
                    window.URL.revokeObjectURL(url);' % (json.dumps(file_content), f'{namespace}.stp')
                    self.export_out.append_display_data(Javascript(jscode))

                self.export.on_click(handle_export)

                self.stdin = Text(description='>>>')

                self.controls = VBox(\
                    [self.export_hbox,
                     HBox([self.uptime, self.memory]), 
                     HBox([self.play_pause, self.reset, self.stop]),
                     self.stdin])

            def get_widget(self):
                return self.controls

            def update(self, uptime, memory):
                if uptime:
                    self.uptime.value = f'Uptime: {uptime}'
                if memory:
                    self.memory.value = f'Memory: {memory}'

            def disable(self):
                for sub_widget in [self.play_pause, self.stop, self.reset, self.stdin]:
                    sub_widget.disabled = True

        # Tab 1: Globals
        class tab_globals:
            def __init__(self):
                self.global_viewer = Output()
                # FIXME: The flickering issue is known, but how do I know a height to set it to?
                # https://ipywidgets.readthedocs.io/en/stable/examples/Using%20Interact.html#Flickering-and-jumping-output

            def get_widget(self):
                return self.global_viewer

            def update(self, globals_json):
                if globals_json is None and len(self.global_viewer.outputs) != 0:
                    # Keep the old data (ex. after cell execution)
                    pass
                elif globals_json is None and len(self.global_viewer.outputs) == 0:
                    self.global_viewer.append_display_data(Markdown(data="*No data available*"))
                else:
                    # NB: What should be done here is a tab_globals.clear_output()
                    # The issue is that this isn't the primary Output widget and so
                    # the clear doesn't act correctly. So we do this 'hack' variant
                    self.global_viewer.outputs = ()
                    self.global_viewer.append_display_data(JSON(data=globals_json, expanded=True, root="globals"))

        # Tab 2: Probes
        class tab_probes:
            def __init__(self):
                self._num_columns = 6
                self.probes = GridBox([], layout=Layout(
                    grid_template_columns='5ch 1fr 1fr 1fr 1fr 1fr'))

            def get_widget(self):
                return self.probes

            def update(self, probes_json):
                if probes_json is None:
                    # TODO: Might be nice to have a `no data` sort of thing
                    return
                p_list = [Label(value=v, style={'font_weight': 'bold'}) for v in (
                    'state', 'name', 'hits', 'min', 'avg', 'max')]
                for d in probes_json:
                    state = Button(button_style='danger', tooltip='Toggle Probe',
                                   icon='check-square' if d['state'] == 'on' else 'square', layout=Layout(width='5ch'))
                    # We need to store which probe this button goes with, since the actual button will change with update,
                    # but the class will remain const
                    state.add_class(f'probe_idx_{d["index"]}')
                    state.on_click(lambda button: monitor_write(button._dom_classes[-1][len("probe_idx_"):]))
                    p_list += [state, Label(value=d['name']), Label(value=str(d['hits'])), Label(
                        value=str(d['min'])), Label(value=str(d['avg'])), Label(value=str(d['max']))]
                self.probes.children = p_list

            def disable(self):
                state_buttons = self.probes.children[self._num_columns::self._num_columns]
                for button in state_buttons:
                    button.disabled = True

        controls_view = tab_controls(self.namespace.name, self.options, self.args)
        globals_view  = tab_globals()
        probes_view   = tab_probes()
        # The Tab Structure
        tab_content = {
            "Controls": controls_view.get_widget(),
            "Globals":  globals_view.get_widget(),
            "Probes":   probes_view.get_widget()
        }
        tab = Tab(children=list(tab_content.values()))
        for i, k in enumerate(tab_content.keys()):
            tab.set_title(i, k)

        # Handle keyboard events within the widget
        keyboard_event = Event(source=tab, watched_events=['keydown'])
        def handle_keydown(event):
            if not p or p.poll() is not None:
                return
            if event['ctrlKey']:
                match event['key']:
                    case 'c':
                        monitor_write('quit')
            else:
                # This means we clicked a special key like 'backspace' or 'arrow'
                if len(event['key']) > 1:
                    return
                # Write the new char to stdin
                try:
                    p.write_to_stdin(event['key'])
                except BrokenPipeError:
                    pass
                controls_view.stdin.value += event['key']
        keyboard_event.on_dom_event(handle_keydown)
        self.kernel.display(tab)

        def update_widget_states(last_updated_monitor=0, monitor_files_owned = True):
            """Update the widgets, but only do so once every 0.5s (more frequent isn't needed for graphics)"""    
            jso = dict()
            try:
                # We need read/write access to the procfs files. If running directly on the host these have no issues BUT
                # if running a container as root then the notebook (current) user in the container can't accesss these controls
                # so the first time we can do a read (monitor_files_owned is false but the file exists), we
                # chown them
                if SSH_DEST and not monitor_files_owned and os.path.exists(proc_path):
                    files = ['monitor_control', 'monitor_status']
                    for f in files:
                        subprocess.run(format_subprocess_args('chown', '--silent', f'{os.getuid()}:{os.getgid()}', f'{proc_path}/{f}'),
                                       stderr=subprocess.DEVNULL)
                    monitor_files_owned = True

                with open(proc_path+"/monitor_status", 'r') as content:
                    # We use json5 to allow for tailing commas in the json format
                    jso = json5.load(content)
            except:
                pass
    
            # Update the internal copy of the globals, for use with python cells
            self.namespace.update_globals(jso.get("globals", None))

            now = time.time()
            if now - last_updated_monitor < 0.5:
                return last_updated_monitor, monitor_files_owned

            # Update tabs content
            controls_view.update(jso.get("uptime", None), jso.get("memory", None))
            globals_view.update(jso.get("globals", None))
            probes_view.update(jso.get('probe_list', None))

            return now, monitor_files_owned

        def write_to_output(content, output_disabled):
            """Write content (a list of line, stream pairs) to the stream (stdout or stderr).
            If output_disabled skip
            Returns True iff the output is disabled i.e the process is not running
            """
            if output_disabled:
                return True
            for line, stream in content:
                s_line = line.decode()
                # Handle jupyter magic lines
                if s_line.startswith(JUPYTER_MAGIC):
                    command = s_line[len(JUPYTER_MAGIC):].strip()
                    if command == "hist_end":
                        # Display the histogram and then clear the
                        # internal repr (since while it exists, isystemtap assumes
                        # it needs to be filled)
                        self.hist.display_hist()
                        self.hist = None
                        continue
                    elif command.startswith("hist_"):
                        # The start of a histogram block is found
                        # The format is hist_func:arg1:arg2:...
                        hist_args = command[len("hist_"):].split(":")
                        self.hist = JHistogram(self.kernel, *hist_args)
                        continue

                # We have an active histogram so take that data and make it pretty
                if self.hist:
                    # s_line is of the form
                    #'value |-------------------------------------------------- count'
                    s_split = s_line.strip().split(" ")
                    # If we're not the heading, record that line
                    if s_split[0] != 'value' and len(s_split) > 3:
                        # FIXME: Looks like when using sprint* we lose the last count
                        value, count = s_split[0], int(s_split[-1])
                        self.hist.record_entry(value, count)
                    continue

                # If we get here, we just have a regular line so output it
                self.kernel.send_response(self.kernel.iopub_socket, 'stream', {
                                          'name': stream, 'text': s_line})
            return False

        output_disabled = False
        last_updated_monitor = time.time() # We only need to update so often
        monitor_files_owned = False

        # Start the main loop
        with ui_events(self.kernel) as ui_poll:
            while p.poll() is None:
                try:
                    # Write any lines to stdout/err if any are ready
                    output_disabled = write_to_output(p.get_lines(), output_disabled)
                    # Get UI events (up to 10 at a time)
                    # NB: Jupyter isn't designed to have UI elements be interactive while a cell is
                    # running, so this polling is very important for the controls widgets
                    ui_poll(10)
                    # Update the widgets states (controls, globals and probes) & update the internal globals (for python usage)
                    last_updated_monitor, monitor_files_owned = update_widget_states(last_updated_monitor, monitor_files_owned)
                except KeyboardInterrupt:
                    # The kernel sent a sigint (I, I), so catch it and pass on to the child
                    # to allow stap to gracefully terminate
                    monitor_write('quit')
            # On that last line, don't hold back even if there is a partial result (not \n terminated)
            output_disabled = write_to_output(p.get_lines(retain_partial_lines=False), output_disabled)
            # Do our best to cleanup after ourselves
            try:
                p = subprocess.run(format_subprocess_args('rm', '-f', f'{self.namespace.name}.ko'))
            except OSError:
                pass

        # Disable the buttons now that we're done
        controls_view.disable()
        probes_view.disable()
        # Give a final update to make sure it's as up to date as we can make it
        update_widget_states()
        return p.returncode == 0

    def _run_python(self):
        status = True
        try:
            old_stdout, old_stderr = sys.stdout.write, sys.stderr.write
            sys.stdout.write = lambda *args: self.kernel.send_response(
                self.kernel.iopub_socket, 'stream', {'name': "stdout", 'text': str(args)})
            sys.stderr.write = lambda *args: self.kernel.send_response(
                self.kernel.iopub_socket, 'stream', {'name': "stderr", 'text': str(args)})
            # Run the script in python with the systemtap globals defined
            # Also add in display and print
            exec(self.script, self.namespace.get_globals() | {
                 "display": self.kernel.display, "print": self.kernel.print})
        except Exception as e:
            self.kernel.error(e)
            status = False
        sys.stdout.write, sys.stderr.write = old_stdout, old_stderr

        return status

    def _run_help(self):
        self.kernel.print('ISystemtap')
        self.kernel.print(
            f'%%{CellExeMode.EDIT}     [ NAMESPACE ] -[ OPTIONS ]\n\tA piece of a script in NAMESPACE')
        self.kernel.print(
            f'%%{CellExeMode.RUN}      [ NAMESPACE ] -[ OPTIONS ]\n\tCombine and run the edit cells in NAMESPACE')
        self.kernel.print(
            f'%%{CellExeMode.SCRIPT}   [ NAMESPACE ] -[ OPTIONS ] [ --args=(ARGUMENTS) ]\n\tRun an entire script. May be give command line args')
        self.kernel.print(
            f'%%{CellExeMode.PYTHON}   [ NAMESPACE ]\n\tRun a python script using the globals from NAMESPACE')
        self.kernel.print(
            f'%%{CellExeMode.EXAMPLES}\n\tRun a python script using the globals from NAMESPACE')
        self.kernel.print(
            f'%%{CellExeMode.PROBES}\n\tList all available probe points matching the given single probe (See stap -L)')
        self.kernel.print(f'%%{CellExeMode.HELP}\n\tRefer to %%help for more information')
        self.kernel.print(
            f'!command   [ ARGUMENTS ]\n\tRun command in a shell')
        self.kernel.print(f'Run !stap --help for more information on available options')
    
    def _run_examples(self):
        title = Text()

        # Query the DB for the possibilities
        con = None
        DB_PATH = STAP_PKGDATADIR + "/examples/metadatabase.db"
        try:
            con = sqlite3.connect(DB_PATH)
        except:
            self.kernel.error(f"Unable to connect to examples database: {DB_PATH}")
            return False
        res = con.execute('SELECT DISTINCT keywords, name from metavirt')
        keywords      = set()  # Possible keywords
        example_names = list() # Possible examples
        for k in res.fetchall():
            keywords |= set(k[0].split(" "))
            example_names.append(k[1])

        keyword_picker = TagsInput(
            value=[],
            allowed_tags=list(keywords),
            allow_duplicates=False
        )
        name = Combobox(
            placeholder='example_script.stp',
            options=example_names,
            ensure_option=True
        )
        search_button = Button(button_style='info',
                               tooltip='Search', icon='search')
        clear_button = Button(button_style='info',
                              tooltip='Clear', icon='refresh')

        # The top control Box
        search_controls = VBox([
            Label(value="Systemtap Example Search", style={
                  'font_weight': 'bold', 'font_size': '1.5em'}),
            HBox([
                Label(value="Name    ", style={'font_weight': 'bold'}),
                name,
                Label(value="Title Contains", style={'font_weight': 'bold'}),
                title
            ]),
            HBox([
                Label(value="Keywords", style={'font_weight': 'bold'}),
                keyword_picker,
            ]),
            HBox([
                search_button, clear_button
            ])
        ])

        # The table, where the results are stored
        table = VBox(layout=Layout(width='50%'))
        # Holds the table and the output of the example (as well as the script viewer)
        side_by_side_viewer = HBox([table])

        SQL_EXTRACTION = 'SELECT title, name, keywords, description, path FROM metavirt WHERE '
        def make_row(title, name, keywords, description, path):
            out = Output(layout=Layout(border='solid 2px', width='50%'))

            full_path = f'{STAP_PKGDATADIR}/examples/{path}/{name}'

            # The controls for running an example
            run     = Button(button_style='info', tooltip='Run', icon='play')
            back    = Button(button_style='info',tooltip='Back', icon='times')
            options = TagsInput(value=[], allow_duplicates=True)
            args    = TagsInput(value=[], allow_duplicates=True)
            controls = VBox([
                HBox([run, back], layout=Layout(display='flex',
                        flex_flow='row', align_items='flex-end')),
                HBox([Label(value="Options", style={
                        'font_weight': 'bold'}), options]),
                HBox([Label(value="Arguments", style={
                        'font_weight': 'bold'}), args])
            ])
            # The example code preview
            code_block = Code(filename=full_path, language="c")
            # TODO: In order to highlight the code we need a pygments lexer
            # class SystemtapLexer(RegexLexer): ... Will end up with the same logic as the codemirror regex highlighting
            # Left for now due to https://github.com/ipython/ipython/issues/11747
            
            def handle_back(_):
                side_by_side_viewer.children = [table]
            back.on_click(handle_back)

            def handle_run(_):
                    # Remove any old run outputs
                    out.outputs = out.outputs[:2] # Just keep controls, code_block
                    # out.outputs = ()
                    # out.append_display_data(controls)
                    # out.append_display_data(code_block)
                    with out, open(full_path, 'r') as example:
                        c = JCell("examplecell", self.kernel)
                        print("Executing")
                        res = c.execute(
                            f'%%script example_{name[:-4].replace("-", "_")} {" ".join(options.value)} { "" if args.value == [] else "--args=(" + ",".join(args.value) + ")" }\n{example.read()}')
                        self.kernel.display(
                            Valid(value=res['status'] == 'ok', readout="Execution Failure"))
            run.on_click(handle_run)


            preview = Button(button_style='info',
                             tooltip='Preview example script', icon='eye')
            def handle_preview(_):
                out.outputs = ()
                out.append_display_data(controls)
                out.append_display_data(code_block)
                side_by_side_viewer.children = [table, out]
            preview.on_click(handle_preview)

            row = VBox(
                children=[
                    Label(value=f'{name}: {title}', style={
                          'font_weight': 'bold', 'font_size': '1.1em'}),
                    HTML(description),  # HTML wraps
                    Label(value=keywords, style={'font_style': 'italic'}),
                    preview
                ],
                layout=Layout(
                    border='solid 2px',
                ))
            return row

        def handle_search(_):
            # SQLite objects created in a thread can only be used in that same thread
            con = sqlite3.connect(DB_PATH)
            res = None
            if name.value != '':
                # If given an exact script, just pull it up by name
                res = con.execute(SQL_EXTRACTION + 'name = ?', (name.value, ))
            else:
                # Otherwise we need to do a proper search
                CONDITON = " AND ".join(
                    ['title LIKE ?'] +  # The title is like the given substring
                    # Each keyword given must be a substring of the one within the DB
                    # Since keywords are 'foo bar baz'. So ex. we need to match keywords LIKE foo AND keywords like bar
                    ['keywords LIKE ?'] * len(keyword_picker.value)
                )
                params = tuple('%'+p+'%' for p in (title.value,
                                                   )+tuple(keyword_picker.value))
                res = con.execute(SQL_EXTRACTION + CONDITON, params)
            table.children = [make_row(*r) for r in res]
            side_by_side_viewer.children = [table]
        search_button.on_click(handle_search)
        name.on_submit(handle_search)
        title.on_submit(handle_search)

        def handle_clear(_):
            title.value = ''
            name.value = ''
            keyword_picker.value = []
            table.children = []
            side_by_side_viewer.children = [table]
        clear_button.on_click(handle_clear)

        self.kernel.display(VBox([search_controls, side_by_side_viewer]))

    def _run_probe_list(self):
        query = Text()
        search_button = Button(button_style='info', tooltip='Search', icon='search')
        clear_button = Button(button_style='info', tooltip='Clear', icon='refresh')
        results = VBox([])
        def list_probes(_):
            # Just visual feedback since the loading can take a second. This serves no real purpose
            progress = IntProgress(value=2, min=0, max=5, step=1,
                                    bar_style='info', orientation='horizontal')
            results.children = [progress]

            rows = []
            p = subprocess.run(format_subprocess_args("stap", '-L', query.value),
                               capture_output=True, text=True, env=SUBPROCESS_ENV)
            progress.value = 4  # Again just a visual thing
            if p.returncode == 0:
                for line in str(p.stdout).split('\n'):
                    pp_end = line.find(' ')
                    if pp_end != -1: # There is a space i.e context variables
                        probe_point, context_vars = line[:pp_end], line[pp_end+1:]
                    else:
                        probe_point, context_vars = line, ""
                    rows.append(VBox([
                        Label(value=probe_point, style={
                              'font_weight': 'bold', 'font_size': '1.1em'}),
                        # We use HTML, since Labels render LaTex (so $arg: ... $arg2 is math mode causing issues)
                        HTML(f'<em>{context_vars}</em>')
                    ]))
            results.children = rows
        search_button.on_click(list_probes)
        query.on_submit(list_probes)

        def clear_list(_):
            query.value = ""
            results.children = []
        clear_button.on_click(clear_list)

        self.kernel.display(VBox([
            Label(value="Systemtap Probe List", style={
                  'font_weight': 'bold', 'font_size': '1.5em'}),
            Label(value="List all available probe points (and context variables) matching the given single probe point. The pattern may include wildcards and aliases", 
                  style={'font_style': 'italic'}),
            HBox([Label(value="probe", style={'font_weight': 'bold'}), query]),
            HBox([search_button, clear_button]),
            results
        ]))

    def execute(self, code):
        if not self._load_script(code):
            status = 'error'
        else:
            status = 'ok'
            match self.mode:
                case CellExeMode.EDIT:
                    actions = [self._validate_syntax,
                               self._validate_symbols_and_types]
                case CellExeMode.RUN:
                    actions = [self._run_namespace]
                case CellExeMode.PYTHON:
                    actions = [self._run_python]
                case CellExeMode.SCRIPT:
                    def add_to_ns():
                        # As a script there are no other cells to stitch,
                        # so we want remove the previous scripts (if they exist)
                        self.namespace.remove_all_cells()
                        self.namespace.add_cell(self)
                        return True
                    actions = [add_to_ns, self._run_namespace]
                case CellExeMode.EXAMPLES:
                    actions = [self._run_examples]
                case CellExeMode.PROBES:
                    actions = [self._run_probe_list]
                case CellExeMode.HELP:
                    actions = [self._run_help]
                case _:
                    actions = []

            for a in actions:
                if not a():
                    status = 'error'
                    break

        return {'status': status,
                'execution_count': self.kernel.execution_count,
                'payload': [],
                'user_expressions': {},
                }
