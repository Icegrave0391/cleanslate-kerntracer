# Language server tests
# Copyright (C) 2022 Red Hat Inc.
#
# This file is part of systemtap, and is free software.  You can
# redistribute it and/or modify it under the terms of the GNU General
# Public License (GPL); either version 2, or (at your option) any
# later version.
from jsonrpc import ReadWriter, JSONRPC2Connection
import sys
import os
import argparse
import unittest
import subprocess
try:
    from unittest import mock
except ImportError:
    import mock
from typing import Union

class Method():
    # General
    EXIT = 'exit'
    INITIALIZE = 'initialize'
    INITIALIZED = 'initialized'
    SHUTDOWN = 'shutdown'
    TEXT_DOCUMENT_DID_CHANGE = 'textDocument/didChange'
    TEXT_DOCUMENT_DID_CLOSE = 'textDocument/didClose'
    TEXT_DOCUMENT_DID_OPEN = 'textDocument/didOpen'
    TEXT_DOCUMENT_DID_SAVE = 'textDocument/didSave'
    # Language Features
    COMPLETION = 'textDocument/completion'


class MockClient():
    def __init__(self):
        self.server_conn = None
        verbose = CMD_ARGS.verbose

        args = [CMD_ARGS.stap_path, '--language-server']
        if verbose > 0:
            args.extend(['-vvv'])
        server = subprocess.Popen(
            args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, universal_newlines=False)
        self.server_conn = JSONRPC2Connection(
            ReadWriter(reader=server.stdout, writer=server.stdin))

        # Setup and open a doccument

        _ = self.server_conn.send_request(
            method=Method.INITIALIZE,
            params=dict(
                rootUri=f'file://{os.getcwd()}',
                capabilities=dict(),
                rootPath=os.getcwd(),
                clientInfo=dict(
                    name='test-language-server-client',
                    processId=os.getpid(),
                    trace='off'
                )
            )
        )

        self.server_conn.send_notification(Method.INITIALIZED, dict())

        self.text_document = dict(
            uri='file://fake_doc.txt',
            languageId='systemtap',
            version=1,
            text=''
        )

        self.server_conn.send_notification(
            method=Method.TEXT_DOCUMENT_DID_OPEN,
            params=dict(textDocument=self.text_document)
        )

    def __del__(self):
        # Close and showdown

        self.server_conn.send_notification(
            method=Method.TEXT_DOCUMENT_DID_CLOSE,
            params=dict(textDocument=dict(uri=self.text_document['uri']))
        )

        self.server_conn.send_notification(Method.SHUTDOWN, dict())

        self.server_conn.send_notification(Method.EXIT, dict())

    def completion_request_full(self, code_snippet, position=None):
        # If position is not provided, assume we're at the snippet end
        if not position:
            lines = code_snippet.split("\n")
            position = dict(line=len(lines)-1, character=len(lines[-1]))

        self.server_conn.send_notification(
            method=Method.TEXT_DOCUMENT_DID_CHANGE,
            params=dict(
                textDocument=dict(
                    uri=self.text_document['uri'],
                    version=1
                ),
                contentChanges=[dict(
                    text=code_snippet,
                )]
            )
        )

        return self.server_conn.send_request(
            method=Method.COMPLETION,
            params=dict(
                textDocument=dict(uri=self.text_document['uri']),
                position=position
            )
        )
    
    def completion_request_inc(self, changes, position):
        # The first change must be Full, and the remainder can be either full or incremental
        assert('range' not in changes[0])

        for v, c in enumerate(changes):
            self.server_conn.send_notification(
                method=Method.TEXT_DOCUMENT_DID_CHANGE,
                params=dict(
                    textDocument=dict(
                        uri=self.text_document['uri'],
                        version=v+1
                    ),
                    contentChanges=[c if isinstance(c, dict) else dict(text=c) ]
                )
            )

        # The insertion position should be specified when testing incremental changes
        assert(position is not None)

        return self.server_conn.send_request(
            method=Method.COMPLETION,
            params=dict(
                textDocument=dict(uri=self.text_document['uri']),
                position=position
            )
        )

class ServerCompletionTests(unittest.TestCase):
    def __init__(self, *args, **kwargs) -> None:
        super().__init__(*args, **kwargs)
        self.client = MockClient() # Each testcase gets its own mockclient

    def labels_match(self, change: Union[str, list], expected, position=None, exactMatch=True, type=None, sync_kind = "full"):
        comp_func = MockClient.completion_request_inc if sync_kind != "full" else MockClient.completion_request_full

        completion_list = comp_func(self.client,
            change, position).get("result", dict())

        completion_items = completion_list.get("items", [])

        # Get the labels (the default insertion text) of type detail (if a type is provided, else everything)
        actual = [ci["label"]
                  for ci in completion_items if not type or ci["detail"] == type]

        # Expected is a subset of actual
        subset_match = all(e in actual for e in expected)
        # We may be ok with just having a subset match, or we might need an exact match
        length_match = not exactMatch or len(actual) == len(expected)

        self.assertTrue(subset_match and length_match,
                        f'\nCompleting "{change}"\nGot: {actual}\nExpected{"" if exactMatch else " Superset Of " }: {expected}')

    def test_basic(self):
        # General completions for an unknown context
        self.labels_match('pr', ['probe', 'private'])
        self.labels_match('func', ['function'])
        self.labels_match('g', ['global'])
        # the private prefix just skips private and completes like normal
        self.labels_match('private pro', ['probe'])
        # Some text/whitespace before the completion doesn't change anything
        self.labels_match('\t\n\n       p', ['probe', 'private'])
        self.labels_match(
            '%%edit foobar\t    \n\n  function foo ()\n{}\n     globa', ['global'])

    def test_probe_comps(self):
        self.labels_match('probe', [])
        # Uses match nodes correctly
        self.labels_match('probe one', ['oneshot'])
        self.labels_match('probe proces', ['process', 'process(', 'process("'])
        # Completing a second token
        self.labels_match('probe process', [
                          'process', 'process(', 'process("'])
        self.labels_match('probe process(12345678).en', ['end', 'end('])
        # Just look for a subset of expected in actual
        self.labels_match('probe process.', ['begin'], exactMatch=False)
        # Doesn't match anything
        self.labels_match('probe twosh', [])

    def test_string(self):
        file_path = os.getenv("TEST_C_FILE")

        process_prefix = 'probe process("'
        # Absolute path completion
        self.labels_match(process_prefix + '/', ['/root'], exactMatch=False)
        self.labels_match(process_prefix + '/li', ['/lib', '/lib64'])
        # Relative path completion
        self.labels_match(process_prefix + 'foo/', ['bar'])
        self.labels_match(process_prefix + 'foo/bar/', ['baz', 'baz.c'])

        # Check the text to be inserted as well, not just the label (textEdit has the same content as insertText so it's covered by this test as well)
        insert_text_test_code = process_prefix + 'foo/ba'
        actual = [(ci["label"], 'foo/ba'+ci["insertText"]) for ci in self.client.completion_request_full(
            insert_text_test_code).get("result", dict(items=[]))["items"]]
        expected = [('bar', 'foo/bar')] # = (label, prefix + insertion text)
        self.assertTrue(all(e in actual for e in expected) and len(actual) == len(expected),
            f'\nCompleting "{insert_text_test_code}"\nGot: {actual}\nExpected: {expected}')
        # TODO: Test with globs once they're supported

        # Test function/statement completion using absolute path
        self.labels_match(f'probe process("{os.getcwd().replace("build", "systemtap")}/systemtap.language_server/foo/bar/baz").function("s',
                          [f'"spam@{file_path}:1"',
                           f'"space@{file_path}:5"'])

    def test_body(self):
        prefix = "probe oneshot, foo, bar, baz { \n"
        # Body completion does not determine the structure, it will just return the available key/atwords/macros/functions
        # first line testing
        self.labels_match(prefix + 'fo', ['for', 'foreach'], type="keyword")
        self.labels_match(
            prefix + '@pr', ['probewrite', 'prometheus_dump_array1'], exactMatch=False)
        self.labels_match(
            prefix + 'ti', ['tid', 'timer_pending'], type="function")

        # Complex line testing
        line = "\t\tif ( cpu() == 0 && getti<CURSOR> > 1140498000)"
        pos = dict(line=1, character=line.find("<CURSOR>"))
        self.labels_match(prefix + line,
                          ['gettimeofday_ms', 'gettimeofday_ns', 'gettimeofday_s', 'gettimeofday_us'], position=pos)

        line = "foreach ([a,b] in foo+ lim"
        self.labels_match(prefix + line, ['limit'])

        # multiline testing
        lines = """
        function foo (a, b, c)
        {
            if (a < 1) return 0
            else if (b < 2) return 1
            else return modu
        """
        self.labels_match(lines, ['module_name', 'module_size'])

    def test_multiple_contexts(self):
        lines = """
        global baz = 42
        
        probe syscallgroup.io = syscall.open,
               syscall.read { groupname = "io" }
        function f(x) { printf("%s\n", x) }
        probe    end
        {
            f("hello world");
        foreach (eg+ in groups)
        @coun
        """
        self.labels_match(lines, ['count'])

    def test_no_completions(self):
        # Global defintions, function signatures and embedded C have no completions
        # TODO: Complete C
        self.labels_match("global x = ", [])
        self.labels_match("function foo (a, ", [])
        self.labels_match("probe", [])
        self.labels_match("function foo () %{ struc ", [])

    def test_local_definitions(self):
        code = """
        global afoo = 42
        
         function abar(x) { printf("%s", x) }
        probe    end
        {
            f("hello world");
        foreach (eg+ in groups)
        a<CURSOR>
        }
            global baz = -1

            global abaz = "hello"
        """
        lines = code.split('\n')
        cursor_line = [idx for idx, l in enumerate(lines) if '<CURSOR>' in l][0]
        pos = dict(line=cursor_line, character=lines[cursor_line].find("<CURSOR>"))
        self.labels_match(code, ['afoo', 'abar', 'abaz'], pos, False)

    def test_target_vars(self):
        code = 'function vars() {}\nprobe kernel.function("vfs_read") { $'
        self.labels_match(code, ['$file','$buf','$count','$pos', '$$vars', '$$locals', "$$parms"])

    def test_incremental_changes(self):
        def make_position(ln, chr):
            return dict(line=ln, character=chr)
        def make_change(text, range_start, range_end = None):
            if range_end is None: range_end = range_start
            return dict(
                text = text,
                range = dict(start = range_start, end = range_end)
            )

        # The first change is a full, and the remainder are incremental
        self.labels_match( [ 'pr', make_change('o', make_position(0,2)) ] , ['probe'], make_position(0, 3), True, None, "incremental")

        # Prepend a new line and then complete the new line under it (between 2 lines)
        """
        global bar = 10
        function f() { ba<CURSOR>
        probe oneshot {}
        """
        self.labels_match(
            [ 'probe oneshot {}',
              make_change('global bar = 10\n', make_position(0,0)),
              make_change('function f() { ba\n', make_position(1,0))
            ],
            ['bar'], make_position(1, 16), False, None, "incremental")

        # Append a new line and then complete the new line before it (between 2 lines)
        """
        probe oneshot {}
        function f() { ba<CURSOR>
        global bar = 10
        """
        self.labels_match(
            [ 'probe oneshot {}',
              make_change('\nglobal bar = 10', make_position(1,0)),
              make_change('function f() { ba\n', make_position(1,0))
            ],
            ['backtrace', 'bdevname', 'big_endian2', 'big_endian4', 'big_endian8',
            'bio_op', 'bio_rw_num', 'bio_rw_str', 'bytes_to_string', 'break'],
             make_position(1, 16), True, None, "incremental") # Don't match bar since its now inside the function body so its not a global

        # Modify the first and last lines and complete between them
        """
        global bar

        function f() { ba<CURSOR>}
        global baz
        """
        self.labels_match(
            [ 'global ham\nfunction f() { ba}\nglobal spam = 42',
              make_change('bar\n', make_position(0, 7), make_position(0, 10)), # replace ham with bar\n
              make_change('baz', make_position(3, 7), make_position(3, 11)),   # replace spam with baz
            ],
            ['bar', 'baz'], make_position(2, 16), False, None, "incremental")

        # Modify the first and last characters and complete between
        """
        global bar
        function f() { ba<CURSOR> }
        global baz
        """
        self.labels_match(
            [ 'flobal bar\nfunction f() { ba }\nglobal bam',
              make_change('g', make_position(0, 0), make_position(0, 1)), # replace f with g
              make_change('z', make_position(2, 9), make_position(2, 10)),   # replace m with z
            ],
            ['bar', 'baz'], make_position(1, 16), False, None, "incremental")
        
        # Remove lines and replace with new lines
        """
        probe one<CURSOR>
        """
        self.labels_match(
            [ 'global bar\nfunction foo() { return 10 }\nglobal baz',
              make_change('', make_position(0, 0), make_position(3, 11)), # remove first 3 lines
              make_change('probe one', make_position(0, 0)),   # insert into start of doc
            ],
            ['oneshot'], make_position(0, 9), False, None, "incremental")

class ServerIntegrationTests(unittest.TestCase):
    def __init__(self, *args, **kwargs) -> None:
        super().__init__(*args, **kwargs)
        self.verbose = CMD_ARGS.verbose
        self.server_conn = None
        self.maxDiff = None # When asserting, show the full diff of actual and expected

    def _test_request(self, request_method: Method, request_params: dict, expected_result: dict):
        response = self.server_conn.send_request(
            request_method, request_params)
        self.assertDictEqual(response.get("result", {}), expected_result)

    def _send_notification(self, notification_method: Method, notification_params: dict = dict()):
        self.server_conn.send_notification(
            notification_method, notification_params)

    def test_basic(self):
        args = [CMD_ARGS.stap_path, '--language-server']
        if self.verbose > 0:
            args.extend(['-'+'v'*self.verbose])
        server = subprocess.Popen(
            args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, universal_newlines=False)
        self.server_conn = JSONRPC2Connection(
            ReadWriter(reader=server.stdout, writer=server.stdin))

        # Request that the server initialize
        self._test_request(
            request_method=Method.INITIALIZE,
            request_params=dict(
                rootUri=f'file://{os.getcwd()}',
                capabilities=dict(),
                rootPath=os.getcwd(),
                clientInfo=dict(
                    name='test-language-server-client',
                    processId=os.getpid(),
                    trace='off'
                )
            ),
            expected_result=dict(
                capabilities=dict(
                    textDocumentSync=2, # Inc Sync
                    completionProvider=dict(
                        resolveProvider=False,
                        triggerCharacters=['.', '/', '@', "$", "*"]
                    )
                ),
                serverInfo=dict(
                    name="systemtap-language-server",
                    version="1.0"
                )
            )
        )

        # Send the server a notification that the initialization was successful.
        # NOTE: Notifications don't have results, we just check their side effects to ensure
        # correctness
        self._send_notification(notification_method=Method.INITIALIZED)

        textDocument = dict(
            uri='file://fake_doc.txt',
            languageId='systemtap',
            version=1,
            text='pr'
        )

        self._send_notification(
            notification_method=Method.TEXT_DOCUMENT_DID_OPEN,
            notification_params=dict(
                textDocument=textDocument
            )
        )

        # By changing the textDocument's text from pr to pro the completion request verifies
        # the change was correct since only 'probe' should be returned, not 'private' as well
        self._send_notification(
            notification_method=Method.TEXT_DOCUMENT_DID_CHANGE,
            notification_params=dict(
                textDocument=dict(
                    uri=textDocument['uri'],
                    version=textDocument['version']+1
                ),
                contentChanges=[dict(
                    text=textDocument['text'] + 'o',
                )]
            )
        )

        # Make sure the unknown methods don't cause issues
        response = self.server_conn.send_request(
            "foobar", dict())
        MethodNotFoundError = -32601
        self.assertEqual(response.get("error", {}).get(
            "code", -1), MethodNotFoundError)

        self._test_request(
            request_method=Method.COMPLETION,
            request_params=dict(
                textDocument=dict(
                    uri=textDocument['uri']
                ),
                position=dict(
                    character=3,
                    line=0
                )
            ),
            expected_result=dict(
                isIncomplete=False,
                items=[dict(
                    detail='keyword',
                    label='probe'
                )]
            )
        )

        self._send_notification(
            notification_method=Method.TEXT_DOCUMENT_DID_CLOSE,
            notification_params=dict(
                textDocument=dict(uri=textDocument['uri'])
            )
        )

        # Technically this should be a request, but its not an issue since we don't need a response here
        self._send_notification(notification_method=Method.SHUTDOWN)

        # Make sure the shutdown worked, since now only exit notifications should
        # be accepted and all others should error
        response = self.server_conn.send_request(
            Method.COMPLETION, dict())
        InvalidRequestError = -32600
        self.assertEqual(response.get("error", {}).get(
            "code", -1), InvalidRequestError)

        self._send_notification(Method.EXIT)

        ret_code = server.wait(timeout=10)
        self.assertEqual(ret_code, 0)


def test_suite(test_completion, test_integration):
    suite = unittest.TestSuite()
    if test_completion:
        suite.addTests([
            ServerCompletionTests('test_basic'),
            ServerCompletionTests('test_probe_comps'),
            ServerCompletionTests('test_string'),
            ServerCompletionTests('test_body'),
            ServerCompletionTests('test_multiple_contexts'),
            ServerCompletionTests('test_no_completions'),
            ServerCompletionTests('test_incremental_changes'),
            ServerCompletionTests('test_local_definitions'),
            ServerCompletionTests("test_target_vars")
        ])

    if test_integration:
        suite.addTest(ServerIntegrationTests('test_basic'))

    return suite


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.description = "Systemtap language server tests"
    parser.add_argument(
        "-v",
        "--verbose",
        help="increase verbosity of test",
        action="count",
        default=0,
    )
    parser.add_argument(
        "--stap-path",
        help="the path to the stap executable",
        type=str,
    )

    global CMD_ARGS
    CMD_ARGS = parser.parse_args()

    runner = unittest.TextTestRunner(failfast=True)
    ENV = dict(
        PATH=os.getenv("TEST_PATH"),
        TEST_C_FILE=os.getenv("TEST_C_FILE")
    )

    with mock.patch.dict(os.environ, ENV):
        res = runner.run(test_suite(
            test_completion=True, test_integration=True))
        sys.exit(0 if res.wasSuccessful() else 1)
