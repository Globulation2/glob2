#!/usr/bin/env python3
"""Bridge newline-delimited stdio MCP to Glob2's loopback HTTP server.

The game must have Settings > MCP server enabled. Desktop environment variables
are inherited unchanged. EOF detaches from the game; forwarded requests are never
retried, even if a connection fails after sending input.
"""
from __future__ import annotations
import argparse
import http.client
import json
import os
import socket
import subprocess
import sys
import time
import threading
import urllib.parse

PROTOCOL = '2025-11-25'
MAX_REQUEST = 1024 * 1024
REQUIRED_TOOLS = frozenset({
    'ui_status', 'ui_screenshot', 'ui_acquire_control', 'ui_release_control',
    'ui_move', 'ui_click', 'ui_mouse_button', 'ui_scroll', 'ui_drag', 'ui_key',
    'ui_type_text', 'ui_release_inputs', 'ui_inspect', 'ui_act', 'ui_wait',
    'ui_renew_control', 'hive_status', 'hive_api', 'hive_run', 'hive_result', 'hive_program',
})


class Bridge:
    def __init__(self, url: str, timeout: float = 35):
        parsed = urllib.parse.urlsplit(url)
        if (parsed.scheme != 'http' or parsed.hostname not in ('127.0.0.1', 'localhost')
                or parsed.username or parsed.password or parsed.query or parsed.fragment
                or parsed.path != '/mcp'):
            raise ValueError('MCP URL must be http://127.0.0.1:PORT/mcp or http://localhost:PORT/mcp')
        self.host, self.port, self.path = parsed.hostname, parsed.port or 80, parsed.path
        self.timeout = timeout
        self.protocol = PROTOCOL

    def forward(self, message, deadline=None):
        wire = json.dumps(message, ensure_ascii=False).encode('utf-8')
        if len(wire) > MAX_REQUEST:
            raise ValueError('Request exceeds 1 MiB')
        remaining = None if deadline is None else deadline - time.monotonic()
        if remaining is not None and remaining <= 0:
            raise TimeoutError('Glob2 MCP readiness deadline exceeded')
        connection = http.client.HTTPConnection(
            self.host, self.port, timeout=self.timeout if remaining is None else remaining)
        expired = threading.Event()
        connected_socket = None

        def expire():
            expired.set()
            # Closing alone can leave the response's buffered reader holding the
            # descriptor. Shutdown also interrupts trickled headers/body reads.
            stream = connected_socket or connection.sock
            if stream is not None:
                try:
                    stream.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
            connection.close()

        timer = None if remaining is None else threading.Timer(remaining, expire)
        if timer is not None:
            timer.daemon = True
            timer.start()
        headers = {'Content-Type': 'application/json', 'Accept': 'application/json, text/event-stream',
                   'MCP-Protocol-Version': self.protocol}
        try:
            if deadline is not None:
                connection.connect()
                connected_socket = connection.sock
                # Deadline shutdown must not make HTTPConnection reopen a socket.
                connection.auto_open = 0
                if expired.is_set():
                    raise TimeoutError('Glob2 MCP readiness deadline exceeded')
            connection.request('POST', self.path, wire, headers)
            if expired.is_set():
                raise TimeoutError('Glob2 MCP readiness deadline exceeded')
            response = connection.getresponse()
            data = response.read(64 * 1024 * 1024 + 1)
            if expired.is_set():
                raise TimeoutError('Glob2 MCP readiness deadline exceeded')
            if response.status == 202 and not data:
                return None
            if response.status != 200:
                raise RuntimeError(f'Glob2 HTTP {response.status}: {data[:512].decode("utf-8", "replace")}')
            if len(data) > 64 * 1024 * 1024:
                raise RuntimeError('Glob2 response exceeds 64 MiB')
            result = json.loads(data)
            if expired.is_set():
                raise TimeoutError('Glob2 MCP readiness deadline exceeded')
            if message.get('method') == 'initialize' and 'result' in result:
                self.protocol = result['result'].get('protocolVersion', self.protocol)
            return result
        except Exception as error:
            if expired.is_set():
                raise TimeoutError('Glob2 MCP readiness deadline exceeded') from error
            raise
        finally:
            if timer is not None:
                timer.cancel()
            connection.close()

    def ready(self, deadline=None):
        reply = self.forward({'jsonrpc': '2.0', 'id': 'launcher-readiness', 'method': 'initialize',
                              'params': {'protocolVersion': PROTOCOL, 'capabilities': {},
                                         'clientInfo': {'name': 'glob2-launcher', 'version': '1'}}}, deadline=deadline)
        if not reply or 'result' not in reply or reply['result'].get('serverInfo', {}).get('name') != 'globulation2-ui':
            raise RuntimeError('Endpoint is not a compatible Globulation 2 MCP server')
        try:
            discovery = self.forward({'jsonrpc': '2.0', 'id': 'launcher-capabilities',
                                      'method': 'tools/list'}, deadline=deadline)
        except ConnectionRefusedError as error:
            raise RuntimeError('Glob2 listener disappeared during discovery; no second game was started') from error
        names = {tool.get('name') for tool in discovery.get('result', {}).get('tools', [])}
        missing = REQUIRED_TOOLS - names
        if missing:
            raise RuntimeError('Running Glob2 lacks required tools (' + ', '.join(sorted(missing))
                               + '); use a compatible build')


def ensure_running(bridge, executable, arguments, cwd, startup_timeout):
    deadline = time.monotonic() + startup_timeout
    try:
        bridge.ready(deadline=deadline)
        print('Glob2 MCP: reusing running game', file=sys.stderr)
        return None
    except ConnectionRefusedError:
        pass  # Only an absent listener authorizes starting another game.
    if time.monotonic() >= deadline:
        raise TimeoutError('Glob2 MCP readiness deadline exceeded')
    # No shell, environment rewriting, pipe ownership, or termination on disconnect.
    print('Glob2 MCP: starting configured game', file=sys.stderr)
    process = subprocess.Popen([executable, *arguments], cwd=cwd, stdin=subprocess.DEVNULL,
                               stdout=sys.stderr, stderr=sys.stderr, start_new_session=True,
                               env=os.environ.copy(), close_fds=True, restore_signals=False)
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f'Game exited during startup (exit {process.returncode})')
        try:
            bridge.ready(deadline=deadline)
            return process
        except ConnectionRefusedError:
            time.sleep(min(0.1, max(0, deadline - time.monotonic())))
    raise RuntimeError('Glob2 MCP startup timed out. Enable Settings > MCP server and verify the URL. The game was left running.')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', default='glob2')
    parser.add_argument('--cwd')
    parser.add_argument('--url', default='http://127.0.0.1:27202/mcp')
    parser.add_argument('--startup-timeout', type=float, default=30)
    parser.add_argument('game_args', nargs=argparse.REMAINDER, help='Game arguments after -- (including --data-dir)')
    args = parser.parse_args(argv)
    if not 0 < args.startup_timeout <= 120:
        parser.error('--startup-timeout must be greater than zero and at most 120 seconds')
    game_args = args.game_args[1:] if args.game_args[:1] == ['--'] else args.game_args
    try:
        bridge = Bridge(args.url)
        ensure_running(bridge, args.executable, game_args, args.cwd, args.startup_timeout)
        bridge.timeout = 35
        output_lock = threading.Lock()
        waits = threading.BoundedSemaphore(8)

        def emit(result):
            encoded = json.dumps(result, ensure_ascii=False)
            with output_lock:
                print(encoded, flush=True)

        def fail(message, error, code=-32000):
            print(f'Glob2 MCP: {error}; request was not retried', file=sys.stderr)
            if not isinstance(message, dict) or 'id' in message:
                emit({'jsonrpc': '2.0', 'id': message.get('id') if isinstance(message, dict) else None,
                      'error': {'code': code, 'message': str(error)}})

        def forward(message, observer=False):
            try:
                result = bridge.forward(message)
                if 'id' in message and result is not None:
                    emit(result)
            except BrokenPipeError:
                pass  # The client detached; never terminate the game.
            except Exception as error:
                fail(message, f'{error}; delivery may be uncertain; do not retry mutations')
            finally:
                if observer:
                    waits.release()

        while True:
            line = sys.stdin.buffer.readline(MAX_REQUEST + 2)
            if not line:
                return 0
            message = None
            try:
                if len(line) > MAX_REQUEST:
                    # Drain this one line; never parse its tail as a mutation.
                    while line and not line.endswith(b'\n'):
                        line = sys.stdin.buffer.readline(MAX_REQUEST + 2)
                    raise ValueError('Request exceeds 1 MiB')
                message = json.loads(line)
                if not isinstance(message, dict):
                    raise ValueError('Expected a JSON-RPC object')
                if (message.get('method') == 'tools/call'
                        and message.get('params', {}).get('name') == 'ui_wait'):
                    if not waits.acquire(blocking=False):
                        fail(message, 'Too many pending waits (maximum eight)')
                        continue
                    try:
                        # Only observers may overtake the serialized forwarding
                        # path. Input/mutation requests retain stdin order.
                        threading.Thread(target=forward, args=(message, True), daemon=True).start()
                    except Exception:
                        waits.release()
                        raise
                else:
                    forward(message)
            except BrokenPipeError:
                return 0
            except Exception as error:
                fail(message, error, -32700 if message is None else -32600)
    except (OSError, RuntimeError, ValueError, http.client.HTTPException) as error:
        print(f'Glob2 MCP: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
