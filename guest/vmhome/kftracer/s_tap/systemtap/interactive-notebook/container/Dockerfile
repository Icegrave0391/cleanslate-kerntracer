# A dockerfile for an isystemtap container
# Copyright (C) 2023 Red Hat Inc.
#
# This file is part of systemtap, and is free software.  You can
# redistribute it and/or modify it under the terms of the GNU General
# Public License (GPL); either version 2, or (at your option) any
# later version.

# If things act up we know 'latest' at time of development was '2023-03-13'
FROM jupyter/minimal-notebook:latest

LABEL maintainer="Systemtap Development Team <systemtap@sourceware.org>"

# Run the following as the user which started the container
USER root

# Variables used by the jupyter base images
ENV NOTEBOOK_PREFIX=/usr/ \
    NOTEBOOK_DIR=/usr/share/systemtap/interactive-notebook \
    JUPYTER_ENABLE_LAB=yes \
    CHOWN_HOME=yes \
    RESTARTABLE=yes \
    NOTEBOOK_ARGS="--ExtensionApp.default_url=lab/tree/ISystemtap.ipynb"
    # By choice we remove --allow-root from the args, this could technically be added back

RUN mkdir -p $NOTEBOOK_DIR
COPY interactive-notebook $NOTEBOOK_DIR
RUN chown -R $NB_UID:$NB_GID  $NOTEBOOK_DIR
# A runtime config script, which is needed if running a rootless container
# Add it to the startup hooks, a dir of files to run at runtime
# See https://jupyter-docker-stacks.readthedocs.io/en/latest/using/common.html#startup-hooks
RUN mkdir -p /usr/local/bin/before-notebook.d/ && \ 
    chmod o+x $NOTEBOOK_DIR/container/runtime_container_config.sh && \
    cp -v $NOTEBOOK_DIR/container/runtime_container_config.sh /usr/local/bin/before-notebook.d/

# Install the isystemtap kernel, LSP and syntax highlighting extension
WORKDIR $NOTEBOOK_DIR
RUN apt-get update --yes && \
    apt-get install --yes jq
RUN ./container/stap-jupyter-install --container-install $NOTEBOOK_PREFIX
RUN fix-permissions $CONDA_DIR && \
    fix-permissions /home/$NB_USER 

# No need for this dir
RUN rmdir /home/$NB_USER/work

# Move to the workdir and get any notebook examples
WORKDIR /home/$NB_USER
RUN cp -p $NOTEBOOK_DIR/*.ipynb /home/$NB_USER/