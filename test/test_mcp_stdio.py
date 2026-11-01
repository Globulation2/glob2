"""Transport and lifecycle contracts for the standard-library MCP launcher."""
import importlib.util
import json
from pathlib import Path
import threading
import time
import subprocess
import sys
import select
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from unittest.mock import Mock, patch

spec = importlib.util.spec_from_file_location('mcp_stdio', Path(__file__).resolve().parents[1] / 'tools/mcp_stdio.py')
launcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(launcher)


class BridgeTests(unittest.TestCase):
    def setUp(self):
        self.received = []
        received = self.received
        action_received = threading.Event()
        self.behavior = {}
        behavior = self.behavior
        self.capabilities = [
            'ui_status', 'ui_screenshot', 'ui_acquire_control', 'ui_release_control',
            'ui_move', 'ui_click', 'ui_mouse_button', 'ui_scroll', 'ui_drag', 'ui_key',
            'ui_type_text', 'ui_release_inputs', 'ui_inspect', 'ui_act', 'ui_wait',
            'ui_renew_control', 'hive_status', 'hive_api', 'hive_run', 'hive_result', 'hive_program',
        ]
        capabilities = self.capabilities

        class Handler(BaseHTTPRequestHandler):
            def do_POST(self):
                message = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
                received.append((message, dict(self.headers)))
                if message.get('method') == 'notifications/initialized':
                    self.send_response(202)
                    self.end_headers()
                    return
                if message.get('method') == 'tools/call':
                    name = message.get('params', {}).get('name')
                    if name == 'ui_wait':
                        action_received.wait(3)
                    elif name == 'ui_act':
                        action_received.set()
                result = {'serverInfo': {'name': 'globulation2-ui'}, 'protocolVersion': '2025-11-25'}
                if message.get('method') == 'tools/list':
                    result = {'tools': [{'name': name} for name in capabilities]}
                body = json.dumps({'jsonrpc': '2.0', 'id': message['id'], 'result': result}).encode()
                mode = behavior.get(message.get('method'))
                if isinstance(mode, float):
                    time.sleep(mode)
                if mode == 'headers':
                    try:
                        self.wfile.write(b'HTTP/1.0 200 OK\r\nX-Trickle: ')
                        for _ in range(100):
                            self.wfile.write(b'x')
                            self.wfile.flush()
                            time.sleep(0.02)
                    except OSError:
                        pass
                    return
                if mode == 'body':
                    self.send_response(200)
                    self.send_header('Content-Length', str(len(body)))
                    self.end_headers()
                    try:
                        for byte in body:
                            self.wfile.write(bytes([byte]))
                            self.wfile.flush()
                            time.sleep(0.02)
                    except OSError:
                        pass
                    return
                self.send_response(200)
                self.send_header('Content-Length', str(len(body)))
                self.end_headers()
                try:
                    self.wfile.write(body)
                except OSError:
                    pass

            def log_message(self, *_):
                pass

        self.server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.bridge = launcher.Bridge(f'http://127.0.0.1:{self.server.server_port}/mcp')

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()

    def test_reuses_compatible_server_without_starting_game(self):
        with patch.object(launcher.subprocess, 'Popen') as popen:
            self.assertIsNone(launcher.ensure_running(self.bridge, 'glob2', [], None, 1))
            popen.assert_not_called()
        self.assertEqual(self.received[0][0]['method'], 'initialize')

    def test_forwarded_mutations_and_notifications_are_sent_once(self):
        message = {'jsonrpc': '2.0', 'id': 42, 'method': 'tools/call',
                   'params': {'name': 'ui_click', 'arguments': {'x': 2, 'y': 3, 'token': 'x'}}}
        self.bridge.forward(message)
        self.assertIsNone(self.bridge.forward({'jsonrpc': '2.0', 'method': 'notifications/initialized'}))
        self.assertEqual(len(self.received), 2)
        self.assertEqual(self.received[0][0], message)
        self.assertEqual(self.received[0][1]['MCP-Protocol-Version'], '2025-11-25')

    def test_incomplete_server_is_rejected_without_starting_another_game(self):
        self.capabilities.remove('hive_run')
        with patch.object(launcher.subprocess, 'Popen') as popen:
            with self.assertRaisesRegex(RuntimeError, 'hive_run'):
                launcher.ensure_running(self.bridge, 'glob2', [], None, 1)
            popen.assert_not_called()

    def test_stdio_wait_does_not_block_the_action_that_satisfies_it(self):
        process = subprocess.Popen([sys.executable, str(Path(launcher.__file__)), '--url',
                                    f'http://127.0.0.1:{self.server.server_port}/mcp'],
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE)
        try:
            messages = [{'jsonrpc': '2.0', 'id': number, 'method': 'tools/call',
                         'params': {'name': name, 'arguments': {}}}
                        for number, name in [(10, 'ui_wait'), (11, 'ui_act')]]
            process.stdin.write(('\n'.join(json.dumps(m) for m in messages) + '\n').encode())
            process.stdin.flush()
            replies = []
            # Read bytewise without buffered lookahead obscuring select readiness.
            for _ in range(2):
                line = b''
                while not line.endswith(b'\n'):
                    self.assertTrue(select.select([process.stdout], [], [], 2)[0],
                                    'A pending wait blocked the following action')
                    line += launcher.os.read(process.stdout.fileno(), 1)
                replies.append(json.loads(line))
            self.assertEqual({r['id'] for r in replies}, {10, 11})
        finally:
            process.stdin.close()
            process.wait(timeout=5)
            process.stdout.close()
            process.stderr.close()

    def test_listening_endpoint_timeout_does_not_start_another_game(self):
        for mode in ('headers', 'body'):
            with self.subTest(mode=mode):
                self.behavior['initialize'] = mode
                started = time.monotonic()
                with patch.object(launcher.subprocess, 'Popen') as popen:
                    with self.assertRaises(TimeoutError):
                        launcher.ensure_running(self.bridge, 'glob2', [], None, 0.15)
                    popen.assert_not_called()
                self.assertLess(time.monotonic() - started, 0.8)

    def test_initialization_and_discovery_share_one_startup_deadline(self):
        self.behavior.update({'initialize': 0.15, 'tools/list': 0.15})
        started = time.monotonic()
        with patch.object(launcher.subprocess, 'Popen') as popen:
            with self.assertRaises(TimeoutError):
                launcher.ensure_running(self.bridge, 'glob2', [], None, 0.22)
            popen.assert_not_called()
        self.assertLess(time.monotonic() - started, 0.7)

    def test_listener_disappearing_during_discovery_does_not_start_another_game(self):
        initialized = {'result': {'serverInfo': {'name': 'globulation2-ui'}}}
        with patch.object(self.bridge, 'forward', side_effect=[initialized, ConnectionRefusedError()]):
            with patch.object(launcher.subprocess, 'Popen') as popen:
                with self.assertRaisesRegex(RuntimeError, 'disappeared'):
                    launcher.ensure_running(self.bridge, 'glob2', [], None, 0.2)
                popen.assert_not_called()

    def test_spawn_and_readiness_use_the_original_probe_deadline(self):
        fake = Mock()
        fake.ready.side_effect = [ConnectionRefusedError(), None]
        process = Mock()
        process.poll.return_value = None
        with patch.object(launcher.subprocess, 'Popen', return_value=process):
            launcher.ensure_running(fake, 'glob2', [], None, 1)
        deadlines = [call.kwargs['deadline'] for call in fake.ready.call_args_list]
        self.assertEqual(len(deadlines), 2)
        self.assertEqual(deadlines[0], deadlines[1])

    def test_url_is_confined_to_loopback_and_mcp_path(self):
        for url in ['https://127.0.0.1/mcp', 'http://evil.example/mcp',
                    'http://localhost/other', 'http://user@localhost/mcp',
                    'http://127.0.0.1/mcp?secret=1']:
            with self.assertRaises(ValueError):
                launcher.Bridge(url)

    def test_failed_forward_does_not_retry(self):
        connection = Mock()
        connection.getresponse.side_effect = ConnectionResetError('lost after send')
        with patch.object(launcher.http.client, 'HTTPConnection', return_value=connection):
            with self.assertRaises(ConnectionResetError):
                self.bridge.forward({'id': 1, 'method': 'tools/call'})
        connection.request.assert_called_once()
        connection.close.assert_called_once()

    def test_startup_preserves_arguments_environment_and_detaches(self):
        fake = Mock()
        fake.ready.side_effect = [ConnectionRefusedError(), None]
        process = Mock()
        process.poll.return_value = None
        with patch.object(launcher.subprocess, 'Popen', return_value=process) as popen:
            result = launcher.ensure_running(fake, '/game path/glob2', ['--data-dir', '/data path'], '/work path', 1)
        self.assertIs(result, process)
        self.assertEqual(popen.call_args.args[0], ['/game path/glob2', '--data-dir', '/data path'])
        self.assertEqual(popen.call_args.kwargs['env'], dict(launcher.os.environ))
        self.assertTrue(popen.call_args.kwargs['start_new_session'])
        process.terminate.assert_not_called()


if __name__ == '__main__':
    unittest.main()
