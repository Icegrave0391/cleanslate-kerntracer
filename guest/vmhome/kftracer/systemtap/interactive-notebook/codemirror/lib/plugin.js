// The jupyterlab extension
// Copyright (C) 2023 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.
// Based on: https://github.dev/WardBrian/jupyterlab-stan-highlight
import './stap'
import { INotebookTracker } from '@jupyterlab/notebook';
import { CodeCell } from '@jupyterlab/cells';

function registerStapFileType(app) {
  app.docRegistry.addFileType({
    name: 'systemtap',
    displayName: 'Systemtap',
    extensions: ['stp'],
    mimeTypes: ['text/x-systemtap'],
  });
}

function highlightStapCell(cell) {
  if (cell && cell instanceof CodeCell) {
    let editor = cell.editor;
    let magic = editor.getLine(0)
    if (magic?.trim().startsWith("%%python")) {
      editor.setOption("mode", "text/x-python");
    }else{
      editor.setOption("mode", "text/x-systemtap");
    }
    // For options https://codemirror.net/5/doc/manual.html#Configuration
      editor.setOption("indentUnit", 2);
      editor.setOption('firstLineNumber', 0);
      editor.setOption('lineNumbers', true);
      editor.setOption('smartIndent', true);    
  }
}

export default [{
  id: 'jupyterlab-stap-highlight',
  autoStart: true,
  requires: [INotebookTracker],
  activate: function (app, tracker) {
    console.log('JupyterLab extension jupyterlab-stap-highlight is activated');
    registerStapFileType(app);

    tracker.currentChanged.connect((tr, nbPanel) => {
      nbPanel.revealed.then((r) => {
        nbPanel.content.widgets.forEach((cell, index, array) => {
          highlightStapCell(cell);
        });
      });
    });

    tracker.activeCellChanged.connect(() => {
      highlightStapCell(tracker.activeCell)
    });

    // highlight initally
    tracker.widgetAdded.connect((tr, nbPanel) => {
      nbPanel.revealed.then((r) => {
        nbPanel.content.widgets.forEach((cell, index, array) => {
          highlightStapCell(cell);
        });
      });
    });
  }
}];
