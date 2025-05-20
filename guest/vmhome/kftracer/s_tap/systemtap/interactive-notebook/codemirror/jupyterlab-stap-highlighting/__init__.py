# Copyright (C) 2023 Red Hat Inc.
#
# This file is part of systemtap, and is free software.  You can
# redistribute it and/or modify it under the terms of the GNU General
# Public License (GPL); either version 2, or (at your option) any
# later version.
def _jupyter_labextension_paths():
    return [{
        'name': 'jupyterlab-stap-highlight',
        'src': 'static',
    }]
