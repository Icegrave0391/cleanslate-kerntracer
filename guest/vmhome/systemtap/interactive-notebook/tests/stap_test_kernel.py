# A testsuite for the isystemtap kernel
# Copyright (C) 2023 Red Hat Inc.
#
# This file is part of systemtap, and is free software.  You can
# redistribute it and/or modify it under the terms of the GNU General
# Public License (GPL); either version 2, or (at your option) any
# later version.
#
# This is based on jupyter_kernel_test and is required since we need to test with specified
# cell_ids (which their test framework does not support)

from unittest import TestCase

from jupyter_client.blocking.client import BlockingKernelClient
from jupyter_client.manager import KernelManager, start_new_kernel
from jupyter_kernel_test import TIMEOUT, ensure_sync, KernelTests

import unittest
import sys

from typing import Tuple, Dict

class ISystemtapKernelTests(TestCase):
    kernel_name = "isystemtap"
    kc: BlockingKernelClient
    km: KernelManager

    @classmethod
    def setUpClass(cls):
        cls.km, cls.kc = start_new_kernel(kernel_name=cls.kernel_name)

    @classmethod
    def tearDownClass(cls):
        cls.kc.stop_channels()
        cls.km.shutdown_kernel()

    flush_channels = KernelTests.flush_channels
    get_non_kernel_info_reply = KernelTests.get_non_kernel_info_reply

    def execute_helper(self, code: str, cell_id: str) -> Tuple[Dict, Dict, Dict]:
        self.flush_channels()
        def client_execute(code, cell_id):
            content = {
                "code": code,
                "silent": False,
                "store_history": False,
                "user_expressions": {},
                "allow_stdin": True,
                "stop_on_error": True,
            }
            msg = self.kc.session.msg("execute_request", content)
            # The magic line which causes all the trouble of not using jupyter_kernel_test
            msg['metadata'] = dict(deletedCells = [], recordTiming=False, cellId=cell_id)
            self.kc.shell_channel.send(msg)
            return msg["header"]["msg_id"]

        msg_id = client_execute(code, cell_id)

        reply = self.get_non_kernel_info_reply(timeout=TIMEOUT)
        busy_msg = ensure_sync(self.kc.iopub_channel.get_msg)(timeout=1)
        self.assertEqual(busy_msg["content"]["execution_state"], "busy")

        output_msgs = []
        while True:
            msg = ensure_sync(self.kc.iopub_channel.get_msg)(timeout=0.1)
            if msg["msg_type"] == "status":
                self.assertEqual(msg["content"]["execution_state"], "idle")
                break
            elif msg["msg_type"] == "execute_input":
                self.assertEqual(msg["content"]["code"], code)
                continue
            output_msgs.append(msg)

        return reply, output_msgs, [msg for msg in output_msgs if msg['header']['msg_type'] in ('stream', 'display_data')]

    def test_isystemtap_basic(self):
        # A basic helloworld script cell (a)
        r, _, stream_output_msgs = self.execute_helper(
            code='%%script helloworld\n\
                probe oneshot {println("Hello World")}',
            cell_id="a")
        # NOTE: The 0th output is always the control panel ***********************
        self.assertEqual(r['content']['status'], 'ok', stream_output_msgs)
        self.assertEqual(stream_output_msgs[1]['content']['name'], 'stdout')
        self.assertEqual(stream_output_msgs[1]['content']['text'], 'Hello World\n')

        # Replace cell (a) with an edit cell, and then with
        # another edit (b) and a run (c) make sure we can stitch cells together
        r, _, stream_output_msgs = self.execute_helper(
            code='%%edit two_parts\n\
                global g = "g"\n\
                function f() { return "f" }',
            cell_id="a")
        self.assertEqual(r['content']['status'], 'ok', stream_output_msgs)

        r, _, stream_output_msgs = self.execute_helper(
            code='%%edit two_parts\n\
                probe oneshot { printf("%s-%s\\n", g, f()) }',
            cell_id="b")
        self.assertEqual(r['content']['status'], 'ok', stream_output_msgs)

        r, _, stream_output_msgs = self.execute_helper(
            code='%%run two_parts',
            cell_id="c")
        self.assertEqual(r['content']['status'], 'ok')
        self.assertEqual(stream_output_msgs[1]['content']['text'], 'g-f\n', stream_output_msgs)

    def test_isystemtap_cmdline_args(self):
        r, _, stream_output_msgs = self.execute_helper(
            code='%%script argtest1 --vp=01000\n\
                probe oneshot {printf("hello\\n")}',
            cell_id="a")
        self.assertEqual(r['content']['status'], 'ok', stream_output_msgs)
        # Race to see if stderr or stdout is output first here
        stdout = 1 if stream_output_msgs[1]['content']['name'] == 'stdout' else 2
        stderr = 1 if stdout == 2 else 2
        self.assertEqual(stream_output_msgs[stdout]['content']['text'], 'hello\n', stream_output_msgs[stdout])
        self.assertIn('Pass 2: analyzed script:', stream_output_msgs[stderr]['content']['text'], stream_output_msgs[stderr])

        r, _, stream_output_msgs = self.execute_helper(
            code='%%script argtest2 --args(1,a)\n\
                probe oneshot {printf("%d%s\\n", $1, @2)}',
            cell_id="a")
        self.assertEqual(r['content']['status'], 'ok', stream_output_msgs)
        self.assertEqual(stream_output_msgs[1]['content']['text'], '1a\n', stream_output_msgs[1])

    def test_isystemtap_cell_lifetimes(self):
        # Make a cell foo and then switch it to bar
        # The foo namespace should be remove and replaced
        r, _, stream_output_msgs = self.execute_helper(
            code='%%edit foo\n\
                probe oneshot {println("Foo")}',
            cell_id="a")
        self.assertEqual(r['content']['status'], 'ok', stream_output_msgs)

        r, _, stream_output_msgs = self.execute_helper(
            code='%%edit bar\n\
                probe oneshot {println("Baz")}',
            cell_id="a")
        self.assertEqual(r['content']['status'], 'ok', stream_output_msgs)

        r, _, stream_output_msgs = self.execute_helper(
            code='%%edit bar\n\
                probe oneshot {println("Bar")}',
            cell_id="a")
        self.assertEqual(r['content']['status'], 'ok', stream_output_msgs)

        r, _, stream_output_msgs = self.execute_helper(
            code='%%run foo',
            cell_id="b")
        # FIXME: How do I feel about this behaviour
        # self.assertEqual(r['content']['status'], 'ok', stream_output_msgs)
        # self.assertEqual(stream_output_msgs[1]['content']['text'], 'Foo\n', stream_output_msgs)

        r, _, stream_output_msgs = self.execute_helper(
            code='%%run bar',
            cell_id="b")
        self.assertEqual(r['content']['status'], 'ok')
        self.assertEqual(stream_output_msgs[1]['content']['text'], 'Bar\n', stream_output_msgs)

    def test_isystemtap_histograms(self):
        # A basic helloworld script cell (a)
        r, _, output_messages = self.execute_helper(
            code="""%%script histo
                global g
                global f
                probe begin {f <<< 1}
                probe timer.ms(10) {
                    if(@count(g) < 5){
                        g <<< 1
                        f <<< @max(f) * 2
                    }else{
                        exit()
                    }
                }
                probe end {
                    println(@hist_linear(g, 0, 2, 1))
                    # println(@hist_linear(g, 0, 2, 1))
                    // println(@hist_linear(g, 0, 2, 1))
                    /* println(@hist_linear(g, 0, 2, 1)) */
                    println(@hist_log(f))
                    println(@hist_log(f)[2])
                }""",
                                    

            cell_id="a")
        self.assertEqual(len(output_messages), 4, output_messages)
        # The linear histogram of g looks good
        self.assertIn('text/plain', output_messages[1].get('content', {}).get('data', {}))
        self.assertIn("""x=array(['0', '1', '2'], dtype='<U1'), y=array([0, 5, 0])""", output_messages[1]['content']['data']['text/plain'])
        self.assertIn("""title='Linear Histogram of g'""", output_messages[1]['content']['data']['text/plain'])
        
        # The next 3 lines are commented out, so we expect a logarithmic histo
        self.assertIn('text/plain', output_messages[2].get('content', {}).get('data', {}))
        self.assertIn("""x=array(['0', '1', '2', '4', '8', '16', '32', '64', '128'], dtype='<U3'), y=array([0, 1, 1, 1, 1, 1, 1, 0, 0])""",
                        output_messages[2]['content']['data']['text/plain'])
        self.assertIn("""title='Logarithmic Histogram of f'""", output_messages[2]['content']['data']['text/plain'])
        # Finally the last one isn't a histo, we just bring a bucket count
        self.assertEqual(output_messages[3]['content']['name'], 'stdout')
        self.assertEqual(output_messages[3]['content']['text'], '0\n')

    def test_isystemtap_python_cells(self):
        r, _, stream_output_msgs = self.execute_helper(
            code="""%%edit python_scripts
                global c = 1
                global arr
                probe timer.s(1) {
                    arr[c, "foo"] = c * 3
                    c++ 
                    if (c == 5) { exit() }
                } """,
            cell_id="a")
        self.assertEqual(r['content']['status'], 'ok', stream_output_msgs)
        # FIXME: I need to wait for the monitor to startup, but this takes too long. How to ensure it runs?

        r, _, stream_output_msgs = self.execute_helper(
            code='%%run python_scripts',
            cell_id="b")
        self.assertEqual(r['content']['status'], 'ok', stream_output_msgs)

        r, _, stream_output_msgs = self.execute_helper(
            code='%%python python_scripts\nprint(c, arr, arr[(1, "foo")])',
            cell_id="c")
        self.assertEqual(r['content']['status'], 'ok', stream_output_msgs)
        # FIXME: The result is 1 second behind the actual, so its technically missing the last one 
        # actual is len 5, so we only have util 4 so globals has 3
        self.assertEqual(stream_output_msgs[0]['content']['text'], """4 {(1, 'foo'): 3, (2, 'foo'): 6, (3, 'foo'): 9} 3\n""", stream_output_msgs)
        # NOTE: No need to test bqplot here, since its the same mechanism as histogram which is already tested. If one works both do

    def test_isystemtap_errors(self):
        # Regular stderr
        r, _, stream_output_msgs = self.execute_helper(
            code='%%edit bad1\nprobe oneshot',
            cell_id="a")
        self.assertEqual(r['content']['status'], 'error', stream_output_msgs)
        self.assertEqual(stream_output_msgs[0]['content']['name'], 'stderr')
        self.assertIn("parse error: expected one of ', { } = +='", stream_output_msgs[0]['content']['text'])

        # Python stderr
        r, _, stream_output_msgs = self.execute_helper(
            code='%%python bad2\nprint(x)',
            cell_id="b")
        self.assertEqual(r['content']['status'], 'error', stream_output_msgs)
        self.assertEqual(stream_output_msgs[0]['content']['name'], 'stderr')
        self.assertEqual("name 'x' is not defined\n", stream_output_msgs[0]['content']['text'])

    def test_isystemtap_bash(self):
        r, _, stream_output_msgs = self.execute_helper(
            code='!echo Hello World',
            cell_id="a")
        self.assertEqual(r['content']['status'], 'ok', stream_output_msgs)
        self.assertEqual(stream_output_msgs[0]['content']['name'], 'stdout')
        self.assertEqual(stream_output_msgs[0]['content']['text'], 'Hello World\n')
    

if __name__ == '__main__':
    runner = unittest.TextTestRunner(failfast=True)

    suite = unittest.TestSuite()
    suite.addTests([
        ISystemtapKernelTests('test_isystemtap_basic'),
        ISystemtapKernelTests('test_isystemtap_cmdline_args'),
        ISystemtapKernelTests('test_isystemtap_cell_lifetimes'),
        ISystemtapKernelTests('test_isystemtap_histograms'),
        ISystemtapKernelTests('test_isystemtap_python_cells'),
        ISystemtapKernelTests('test_isystemtap_errors'),
        ISystemtapKernelTests('test_isystemtap_bash')
        ])

    res = runner.run(suite)
    sys.exit(0 if res.wasSuccessful() else 1)
