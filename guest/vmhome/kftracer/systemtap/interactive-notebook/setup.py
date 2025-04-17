# A basic setup script for installing the isystemtap module
# Copyright (C) 2023 Red Hat Inc.
#
# This file is part of systemtap, and is free software.  You can
# redistribute it and/or modify it under the terms of the GNU General
# Public License (GPL); either version 2, or (at your option) any
# later version.
from distutils.core import setup

setup(name='ISystemtap',
      version='1.0',
      description='An interactive IPython systemtap notebook',
      author='Systemtap Development Team',
      url='https://sourceware.org/git/systemtap.git',
      packages=['isystemtap']
     )