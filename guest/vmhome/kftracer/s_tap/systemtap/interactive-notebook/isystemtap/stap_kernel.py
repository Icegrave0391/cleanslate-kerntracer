# systemtap jupyter kernel implementation
# Copyright (C) 2023 Red Hat Inc.
#
# This file is part of systemtap, and is free software.  You can
# redistribute it and/or modify it under the terms of the GNU General
# Public License (GPL); either version 2, or (at your option) any
# later version.
#
# The kernel borrows some functions from generic functions from
# the jupyter metakernel, https://github.com/Calysto/metakernel

import base64
import logging
import sys
from .stap_jobjects import *
from ipywidgets import register_comm_target
from ipywidgets.widgets.widget import Widget
import ipykernel
assert ipykernel.__version__ > '6.12', \
    f'The ipykernel version must be at least 6.12, where cellIds are introduced, the current version is {ipykernel.__version__}'
from ipykernel.kernelbase import Kernel
from ipykernel.kernelapp import IPKernelApp
from ipykernel.comm import CommManager
import io
import os
import time
# File is created at build time
try:
    from .constants import STAP_VERSION
except:
    STAP_VERSION = '0.0'

class SystemtapKernel(Kernel):
    implementation = 'isystemtap'
    implementation_version = '1.0'
    language = 'Systemtap'
    language_version = STAP_VERSION
    language_info = {
        'name': 'systemtap',
        'mimetype': 'text/x-systemtap',
        'file_extension': '.stp',
        'codemirror_mode': 'systemtap'
    }
    banner = "ISystemtap Kernel"
    debugger = False  # Kernel does not support debugging in the notebook
    help_links = [
        {"text": "Systemtap Wiki", "url": "https://sourceware.org/systemtap/"},
        {"text": "bqplot", "url": "https://bqplot.github.io/bqplot/api/pyplot/"}
    ]

    @classmethod
    def run_as_main(cls):
        IPKernelApp.launch_instance(kernel_class=cls)

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

        try:
            sys.stdout.write = self.write
        except:
            pass

        # We log to both a file (DEBUG) and stderr (INFO)
        if self.log is None:
            self.log = logging.Logger("stap_kernel")
        logdir = f'{os.getenv("HOME")}/.systemtap/jupyter/logs'
        if not os.path.exists(logdir):
            os.mkdir(logdir)
        logfile = f'{logdir}/kernel_{time.strftime("%Y%m%d_%H%M%S")}.log'
        f_handler = logging.FileHandler(logfile, mode='w')
        f_handler.setFormatter(logging.Formatter(
            '%(asctime)s - %(levelname)s - %(filename)s::%(funcName)s - %(message)s'))
        f_handler.setLevel(logging.DEBUG)
        self.log.addHandler(f_handler)
        # Jupyter plays some games with sys.stderr so we need to open fd=2 in order to log to the terminal
        s_handler = logging.StreamHandler(io.TextIOWrapper(io.BufferedWriter(io.FileIO(2, mode='w'))))
        s_handler.setFormatter(logging.Formatter(
            '[%(levelname).1s %(asctime)s ISystemtapKernel] %(message)s'))
        s_handler.setLevel(logging.INFO)
        self.log.addHandler(s_handler)
        self.log.info(f'For the full log see {logfile}')

        self.jnamespaces = dict() # namespace name -> JNamespace

        # Setup communication channels, which the kernel uses to interact
        # with widgets
        self.shell = None
        self.comm_manager = CommManager(parent=self, kernel=self, shell=self.shell)
        register_comm_target(self)
        comm_msg_types = ['comm_open', 'comm_msg', 'comm_close']
        for msg_type in comm_msg_types:
            self.shell_handlers[msg_type] = getattr(
                self.comm_manager, msg_type)

    def display(self, *objects, **kwargs):
        """Display one or more objects using rich display.
        Supports a `clear_output` keyword argument that clears the output before displaying.
        """
        if kwargs.get('clear_output'):
            self.send_response(self.iopub_socket, 'clear_output',
                               {'wait': True})

        for item in objects:
            if isinstance(item, Widget):
                data = {
                    'text/plain': repr(item),
                    'application/vnd.jupyter.widget-view+json': {
                        'version_major': 2,
                        'version_minor': 0,
                        'model_id': item._model_id
                    }
                }
                content = {
                    'data': data,
                    'metadata': {}
                }
                self.send_response(
                    self.iopub_socket,
                    'display_data',
                    content
                )
            else:
                try:
                    data = _formatter(item)
                except Exception as e:
                    self.error(e)
                    return
                content = {
                    'data': data[0],
                    'metadata': data[1]
                }
                self.send_response(
                    self.iopub_socket,
                    'display_data',
                    content
                )

    def print(self, *objects, **kwargs):
        """Print objects to the iopub stream, separated by sep and followed by end
        Items can be strings or Widget instances.
        """
        for item in objects:
            if isinstance(item, Widget):
                self.display(item)

        objects = [o for o in objects if not isinstance(o, Widget)]
        message = format_message(*objects, **kwargs)

        stream_content = {
            'name': 'stdout', 'text': message}
        self.send_response(self.iopub_socket, 'stream', stream_content)

    def write(self, message):
        """Write message directly to the iopub stdout with no added end character."""
        stream_content = {
            'name': 'stdout', 'text': message}
        self.send_response(self.iopub_socket, 'stream', stream_content)

    def error(self, *objects, **kwargs):
        """Print objects to stdout, separated by sep and followed by end
        Objects are cast to strings.
        """
        message = format_message(*objects, **kwargs)
        self.log.info(f'Error: {message}')
        stream_content = {
            'name': 'stderr',
            'text': message
        }
        self.send_response(self.iopub_socket, 'stream', stream_content)

    def do_execute(self, code, silent=False, store_history=False, user_expressions=None,
                   allow_stdin=False, *, cell_id=None):
        """Execute a cell (with cell_id) containing code.
        Disregard other user arguments
        """
        # NB: ipykernel.kernelbase::_accepts_cell_id requires cell_id in do_execute to be VAR_KEYWORD
        # So in the above the '*' IS REQUIRED
        # As of 2022: Jupyterlab supports cell_id but notebook does not
        cell = JCell(cell_id, kernel=self)
        return cell.execute(code)

    def do_complete(self, code, cursor_pos):
        if cursor_pos is None:
            cursor_pos = len(code)

        if len(code) > cursor_pos:
            code = code[:cursor_pos]

        line = code.split('\n')[-1]
        matches = []
        cursor_start = cursor_pos
        # We complete magics here and the remainder in the Language Server
        if len(line) >= 2 and line[:2] == "%%":
            magic_command = line[2:].split(" ")
            # Try and complete the magic
            if len(magic_command) == 1:
                cursor_start = cursor_pos - len(magic_command[0])
                matches = (CellExeMode.EDIT, CellExeMode.EXAMPLES, CellExeMode.HELP,
                                       CellExeMode.PROBES, CellExeMode.PYTHON, CellExeMode.RUN, CellExeMode.SCRIPT)
            else:
                # We can try and offer namespace matching
                cursor_start = cursor_pos - len(magic_command[1])
                matches = [ns for ns in self.jnamespaces.keys() if ns.startswith(magic_command[1])]

        return {
            'matches': matches,
            'cursor_start': cursor_start,
            'cursor_end': cursor_pos,
            'status': 'ok',
            'metadata': {}
        }

    def do_shutdown(self, restart):
        """Shut down the app gracefully"""
        # Shutdown any running namespace (only 1 can run at a time)
        for ns in self.jnamespaces.values():
            try:
                with open(f'/proc/systemtap/{ns.mod_name}/monitor_control', 'w') as f:
                    f.write('quit')
                    f.flush()
            except OSError:
                pass
        if restart:
            self.log.info("Restarting kernel...")
        return {'status': 'ok', 'restart': restart}

def _formatter(data):
    reprs = {}
    reprs['text/plain'] = repr(data)

    lut = [("_repr_png_", "image/png"),
           ("_repr_jpeg_", "image/jpeg"),
           ("_repr_html_", "text/html"),
           ("_repr_markdown_", "text/markdown"),
           ("_repr_svg_", "image/svg+xml"),
           ("_repr_latex_", "text/latex"),
           ("_repr_json_", "application/json"),
           ("_repr_javascript_", "application/javascript"),
           ("_repr_pdf_", "application/pdf")]

    for (attr, mimetype) in lut:
        obj = getattr(data, attr, None)
        if obj:
            reprs[mimetype] = obj

    format_dict = {}
    metadata_dict = {}
    for (mimetype, value) in reprs.items():
        metadata = None
        try:
            value = value()
        except Exception:
            pass
        if not value:
            continue
        if isinstance(value, tuple):
            metadata = value[1]
            value = value[0]
        if isinstance(value, bytes):
            try:
                value = value.decode('utf-8')
            except Exception:
                value = base64.encodestring(value)
                value = value.decode('utf-8')
        try:
            format_dict[mimetype] = str(value)
        except:
            format_dict[mimetype] = value
        if metadata is not None:
            metadata_dict[mimetype] = metadata
    return (format_dict, metadata_dict)

def format_message(*objects, **kwargs):
    """
    Format a message like print() does.
    """
    objects = [str(i) for i in objects]
    sep = kwargs.get('sep', ' ')
    end = kwargs.get('end', '\n')
    return sep.join(objects) + end

