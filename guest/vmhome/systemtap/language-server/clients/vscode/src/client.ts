/* -------------------------------------------------------------------------
 * Based on https://github.com/openlawlibrary/pygls/tree/master/examples/json-extension
 *  see: https://code.visualstudio.com/api/language-extensions/language-server-extension-guide
 * ----------------------------------------------------------------------- */
"use strict";

import { ExtensionContext, workspace } from "vscode";
import {
    LanguageClient,
    LanguageClientOptions,
    ServerOptions,
} from "vscode-languageclient/node";

let client: LanguageClient;

export function activate(context: ExtensionContext): void {
    let clientOptions: LanguageClientOptions = {
        // Register the server for stp documents
        documentSelector: [
            { scheme: "file", language: "systemtap" },
        ],
        // Notify the server about file changes to '.clientrc files contain in the workspace
        synchronize: {
            fileEvents: workspace.createFileSystemWatcher("**/.clientrc"),
        }
    };

    const serverOptions: ServerOptions = {
        command: "stap",
        args: ["--language-server"],
    };

    client = new LanguageClient(
        'systemtap-lsp-client',
        'Systemtap Language Client',
        serverOptions,
        clientOptions
    );

    client.start();
}

export function deactivate(): Thenable<void> {
    return client ? client.stop() : Promise.resolve();
}
