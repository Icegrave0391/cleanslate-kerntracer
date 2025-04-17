# Language Server
*This document is still a WIP*

## Getting Started
Welcome to the Systemtap language server guide. The language server implements the [language-server-protocol](https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/) and currently supports the following features
* **Lifecycle messages**
  * initialize
  * initialized
  * shutdown
  * exit 
* **Document Synchronization**
  * textDocument/DidOpen
  * textDocument/DidChange (Full and incremental synchronization)
  * textDocument/DidSave
  * textDocument/DidClose
* **Language Featuress**
  * textDocument/completion

*When features are added, make sure to update the above*

## Usage
The language-server, which can be run with `stap --language-server`, will start a process that will communicate 
with the **parent** language-server-client using **stdio**. Below describes some tested clients, and how to set them up to run the language server

### Vim
- Install the [Minimalist Vim plugin manager](https://github.com/junegunn/vim-plug)
```bash
# For unix installation
$ curl -fLo ~/.vim/autoload/plug.vim --create-dirs \
    https://raw.githubusercontent.com/junegunn/vim-plug/master/plug.vim
```
- Add the following to .vimrc
```vim
" The Vim Language-Server plugins
call plug#begin()
Plug 'prabirshrestha/vim-lsp'
Plug 'prabirshrestha/asyncomplete.vim'
Plug 'prabirshrestha/asyncomplete-lsp.vim'
call plug#end()

" Optional logs
" let g:lsp_log_verbose = 1
" let g:lsp_log_file = expand('LOG_FILE_NAME.log')

" Tab Completion
" Tab and Shift-Tab back and forth through completion options
" Tab Ctrl-y to accept a completion
inoremap <expr> <Tab> pumvisible() ? "\<C-n>" : "\<Tab>"
inoremap <expr> <S-Tab> pumvisible() ? "\<C-p>" : "\<S-Tab>"
inoremap <expr> <cr> pumvisible() ? "\<C-y>\<cr>" : "\<cr>"

if (executable('stap'))
    autocmd User lsp_setup call lsp#register_server({
        \ 'name': 'systemtap-language-server',
        \ 'cmd': {server_info->['stap', '--language-server']},
        \ 'whitelist': ['stp']
        \ })
endif
```
- In vim command mode execute `PlugInstall`
- For more details
    - https://github.com/prabirshrestha/asyncomplete.vim
    - https://github.com/prabirshrestha/vim-lsp/blob/master/doc/vim-lsp.txt

### Emacs
* Install [lsp-mode](https://emacs-lsp.github.io/lsp-mode/page/installation/)  
*Using MELPA*  
`M-x` package-install `RET` lsp-mode `RET`
<!-- * Install: https://company-mode.github.io/ (for completion) DO I NEED THIS -->
* Add the following to your emacs [init](https://www.gnu.org/software/emacs/manual/html_node/emacs/Init-File.html) file, and make sure that `lsp-mode, systemtap-mode, company` are installed 
```lisp
(require 'lsp-mode)       ; The language-server client
(require 'systemtap-mode) ; Provides the major mode - systemtap-mode
(require 'company)        ; The library response for the code completion frontend

; Register the systemtaptap mode with the lsp-mode
(with-eval-after-load 'lsp-mode
 (with-eval-after-load 'systemtap-mode
  (add-to-list 'lsp-language-id-configuration '(systemtap-mode . "systemtap"))
 )
)

; Configurations
(setq company-idle-delay            0.0 ; How fast to response to typed input
      company-minimum-prefix-length 1   ; The prefix length before auto completing
      lsp-idle-delay                0.1 ; stap --language-server is fast
)

(lsp-register-client
 (make-lsp-client :new-connection (lsp-stdio-connection '("stap" "--language-server"))
                  :activation-fn (lsp-activate-on "systemtap")
                  :server-id 'stap-ls))
```
* To start the language server run `M-x` lsp while a .stp file is open
* See the [company docs](https://company-mode.github.io/manual/Getting-Started.html) for more details, and note that in order to **see the documentation** on a highlighted completion use `C-h` or `<F1>`

### Jupyter
* Install the [Jupyter LSP extension](https://github.com/jupyter-lsp/jupyterlab-lsp)
```bash
$ pip install jupyterlab-lsp
```

* Make a config file
```json
{
  "LanguageServerManager": {
    "language_servers": {
      "stap-language": {
        "version": 2,
        "argv": ["stap", "--language-server"],
        "debug_argv": ["stap", "--language-server", "-v"],
        "languages": ["systemtap"],
        "mime_types": ["text/systemtap", "text/x-systemtap"],
        "display_name": "systemtap-language-server"
      }
    }
  }
}
```
* Copy the config file to the jupyter config path, using something like the following
then enable the extension
```bash
#!/bin/bash
CONFIG_FILE="jupyter_stap_lsp.json"
JCONFIG=`jupyter --config`
cp $CONFIG_FILE "$JCONFIG/jupyter_server_config.d/$CONFIG_FILE"

jupyter server extension enable --user --py jupyter_lsp
```

### VSCode
*This is the development usage, and a proper vscode extension should be published*

#### Development Directory Structure
```
vscode-workspace-root-directory
├── .vscode
│   ├── launch.json            // Launch configuration
│   ├── settings.json
│   └── tasks.json
│
├── src                        // Language Client
│   └── client.ts              // Language Client entry point
│
├── package.json               // The extension manifest
│
└── tsconfig.json              // TypeScript compiler options
```

#### Usage
*The above [files](./clients/vscode/) should be in the root of the vscode current working directory*
1. Run `npm install package.json`  
2. Open debug view (`ctrl + shift + D`)
3. Select `Launch Client` and press `Start Debbuging` (Play Button / F5)

### Eclipse
*This is the development usage, and a proper eclipse plugin should be published*

* Open the [clients/eclipse](./clients/eclipse/) directory as a project
* Right click and `Run As > Eclipse Application`
* In the new window, open a project and open a .stp file


*If you use a another client, please feel free to submit a patch to the above with the added usage recipe*