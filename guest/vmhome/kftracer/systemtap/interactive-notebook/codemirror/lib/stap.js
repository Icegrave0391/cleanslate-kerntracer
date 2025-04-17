// The stap highlighting rules
// Copyright (C) 2023 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.
import * as CodeMirror from 'codemirror';
import 'codemirror/addon/mode/simple';

CodeMirror.defineSimpleMode("systemtap", {
  // The start state contains the rules that are intially used
  // Rules are matched in order. I am using the same order as vscode/atom
  start: [
    // comments
    { regex: /\/\/\/?.*$/, token: 'comment' },
    { regex: /#.*$/, token: "comment" },
    { regex: /\/\*/, token: 'comment', push: 'comment_block' },
    
    // embedded C
    { regex: /\%{/, token: "meta", mode: { spec: "clike", end: /\%}/}},

    // numbers
    // octal number
    { regex: /(0)([0-7]+)/, token: ["string", "number"] },
    // hex number
    { regex: /(0x)([0-9a-fA-F]+)/, token: ["string", "number"] },
    // numeric args
    { regex: /\$#/, token: "number" },
    { regex: /\$\d+/, token: "number" },
    // decimal number
    { regex: /\d+/, token: "number" },

    // string
    { regex: /"/, token: 'string', push: 'string' },
    
    // TODO: probe signatures could be highlighted
    {
      regex: /probe/,
      token: "keyword",
    },

    // functions (1 group per token)
    {
      regex: /(function)(\s+)([A-Za-z\$_][0-9A-Za-z_\$]*)/,
      token: ["keyword", null, "variable-2"],
    },

    // keywords
    // Should I add (?<!\.) I can't let it start with . since .return probes exist
    {
      regex: /(\b)(break|continue|return|next|delete|try|catch|while|for|foreach|in|limit|if|else|global|function|private|probe)(\b)/,
      token: [null, "keyword", null]
    },

    //macros & atwords
    {
      regex: /@\w+/,
      token: "atom"
    },

    // types
    {
      regex: /(\b)(string|long)(\b)/,
      token: "keyword"
    },

    // Magic commands
    {
      regex: /^%%.*$/,
      token: "string-2"
    },

    // variables
    {
      regex: /\b[A-Za-z\$_][0-9A-Za-z_\$]*\b/,
      token: "variable"
    },

    // operators
    {
      regex: /[\*\/\%\+\-\&\^!=<>~|.]=?|[=!~:?]/,
      token: "operator"
    },

    {
      regex: /\{%\{%\(/,
      indent: true
    },
    {
      regex: /\}%\}%\)/,
      dedent: true
    },

    // punctuation
    { regex: /(\;|\||,)/, token: "punctuation" }
  ],

  comment_block: [
    { regex: /\/\*/, token: 'comment', push: 'comment_block' },
    // this ends and restarts a comment block. but need to catch this so
    // that it doesn\'t start _another_ level of comment blocks
    { regex: /\*\/\*/, token: 'comment' },
    { regex: /(\*\/\s+\*(?!\/)[^\n]*)|(\*\/)/, token: 'comment', pop: true },
    // Match anything else as a character inside the comment
    { regex: /./, token: 'comment' },
  ],

  string: [
    { regex: /"/, token: 'string', pop: true },
    { regex: /./, token: 'string' }
  ],

  // probepoints: [
  //   { regex: /0[0-7]+/, token: "number" },
  //   { regex: /0x[0-9a-fA-F]+/, token: "number" },
  //   { regex: /\d+/, token: "number" },
  //   { regex: /"/, token: 'string', push: 'string' },
  //   // (?={) looksahead for { 
  //   { regex: /.*(?={)/, token: 'comment', pop: true },
  // ],

  meta: {
    closeBrackets: { pairs: "()[]\{\}\"\"%\{\}%(%)" },
    dontIndentStates: ['comment_block'],
    electricInput: /^\s*\}$/,
    blockCommentStart: '/*',
    blockCommentEnd: '*/',
    lineComment: '//',
    fold: 'brace'
  }
});

CodeMirror.defineMIME('text/x-systemtap', 'systemtap');
CodeMirror.defineMIME('text/systemtap', 'systemtap');

CodeMirror.modeInfo.push({
  ext: ['stp'],
  mime: "text/x-systemtap",
  mode: 'systemtap',
  name: 'systemtap'
});
