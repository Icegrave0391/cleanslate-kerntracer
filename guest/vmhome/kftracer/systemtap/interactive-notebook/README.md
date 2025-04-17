# ISystemtap
Welcome to ISystemtap, to get started check the [usage](#usage) below and tryout the ISystemtap.ipynb

## Usage
### Local usage
Once ISystemtap is installed just run `jupyter-lab [notebook_name.ipynb]` and then select a new or existing ISystemtap notebook. For installation see [below](#local-installation)

### Container usage
No local installation of ISystemtap is needed. Once systemtap is installed the following can be run.
It is suggested to pull the pre-built image from `quay.io`, but it can optionally be locally built as well
```bash
stap-jupyter-container [--repo REPOSITORY] [--image IMAGE] [--tag TAG] [--keyname KEYNAME] --{run, pull, build, publish, remove}
```
* --keyname: The name of the key to use for ssh'ing from the container to the host to run the stap commands. There should exist both "keyname" and "keyname.pub" files
* --run: Runs the image (if not pulled / built locally pull it from `quay.io`)
* --pull: Pull the built image (Published to `quay.io/systemtap/isystemtap`)
* --build: Do a local container build
* --publish [For maintainers only]: Publish the image to the public repo
* --remove: Remove the local copy of the image

#### TL;DR
To get started just run `stap-jupyter-container --run`

#### Running as a superuser
Running the container as a superuser means that stap scripts will be run as root. Therefore there are 2 prerequisites
* Make sure to add `PermitRootLogin without-password` to `/etc/ssh/sshd_config`. Note this does not mean there is no authentication it means that logging in is only possible using a fallback method (in this case a public key authentication).
* Make sure that systemtap is installed by root

## Local Installation
Run `stap-jupyter-install`. This will install the ISystemtap kernel, syntax highlighting and language-server dependencies in `~/.systemtap/jupyter` (and the jupyter config directories). To uninstall add the `--remove` flag.

## Development Directory Structure
```
interactive-notebook
├── codemirror                      // Jupyterlab extension for syntax highlighting
│   ├── jupyterlab-stap-highlighting
│   │   └── ...
│   ├── lib
│   │   └── ...
│   └── package.json
│
├── container                       // Container which wraps up the pythonic install elements
│   ├── Dockerfile
│   └── runtime_container_config.sh
│
├── setup.py                        // setup for module. Included in stap-jupyter-install
├── isystemtap                      // the jupyter kernel source and config file
│   ├── *.py
│   └── kernel.json
│
├── tests                           // Some automated and manual tests (Note: to test the dir must be writable)
│   └── ...
│
├── ISystemtap.ipynb                // An introductory notebook example                        
│
├── stap-jupyter-install.in         // Used to generate the local install script
│
├── stap-jupyter-container.in       // Used to generated container management script
│
├── jupyter_stap_lsp.json           // The language server config file
│
└── requirements.txt                // Python reqs
```

## FAQ
1. What if something goes wrong, and a systemtap kernel module is left running after ISystemtap shuts down
    * All you need to do is kill the process and run `sudo staprun -d MODULE_NAME` (this can be checked in `/proc/systemtap`)
2. How do I build from source?
    * The same as normal. `make; make install` followed by one of the above `stap-jupyter-*` commands
3. What does  Error: OCI runtime error: crun: error creating systemd unit `libpod-*.scope: got failed` mean
    * Some file systems have issues with `podman`, try running as a superuser
4. **I'm a maintainer who wants to publish the latest image**; how do I do this?
    * After building and installing systemtap run `stap-jupyter-container --build && stap-jupyter-container --publish`
    * After the build you will be prompted for a `quay.io` login. This will be followed by the image being published to `quay.io/systemtap/isystemtap`