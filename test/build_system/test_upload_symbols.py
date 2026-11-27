"""tools/symbols/upload_symbols.py: the release workflows' crash symbol upload."""

import gzip
import hashlib
import io
import json
import os
import struct
import sys
import tempfile
import unittest
import urllib.error
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'symbols'))
import upload_symbols  # noqa: E402

# Stands in for dump_syms: writes a .sym for the input, named after it, and
# records the --arch it was asked for. A file whose name contains "stripped"
# gets no FUNC records; "mingw" gets an empty MODULE name, as MinGW binaries do.
FAKE_DUMP_SYMS = r'''#!/usr/bin/env python3
import sys
from pathlib import Path
args = sys.argv[1:]
path = Path(args[0]); out = Path(args[args.index('--output') + 1])
arch = args[args.index('--arch') + 1] if '--arch' in args else 'x86_64'
name = '' if 'mingw' in path.name else path.name
body = ['MODULE Linux %s %s %s' % (arch, ('A' if arch == 'x86_64' else 'B') * 32 + '0', name),
        'INFO CODE_ID 0EE707C2F0B5 %s' % path.name]
if 'stripped' not in path.name:
    body.append('FUNC 1000 10 0 main')
out.write_text('\n'.join(body) + '\n')
'''


class Response(io.BytesIO):
    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False


class UploadSymbolsTests(unittest.TestCase):
    def setUp(self):
        self.work = Path(tempfile.mkdtemp())
        tool = self.work / 'dump_syms'
        tool.write_text(FAKE_DUMP_SYMS)
        tool.chmod(0o755)
        self.env = mock.patch.dict(os.environ, {'GLOB2_DUMP_SYMS': str(tool), 'CRASH_SYMBOL_UPLOAD_TOKEN': 'secret'})
        self.env.start()
        self.addCleanup(self.env.stop)
        self.requests = []

    def binary(self, name, data=b'\x7fELF'):
        path = self.work / name
        path.write_bytes(data)
        return path

    def opener(self, *answers):
        answers = list(answers)

        def open_(request, timeout):
            body = gzip.decompress(request.data).decode()
            self.requests.append((request.full_url, request.headers, body))
            answer = answers.pop(0) if answers else 201
            if answer != 201:
                raise urllib.error.HTTPError(request.full_url, answer, 'no', {}, io.BytesIO(b'{"code":"x"}'))
            first = body.split('\n', 1)[0].split(' ')
            return Response(json.dumps({'kind': 'breakpad', 'module': first[4] if len(first) > 4 else 'glob2.wasm',
                                        'debugId': first[3] if len(first) > 3 else 'x', 'version': '0.11.8.1',
                                        'replaced': False}).encode())
        return open_

    def run_main(self, *argv, opener=None, sleeps=None):
        stdout, stderr = io.StringIO(), io.StringIO()
        with mock.patch('sys.stdout', stdout), mock.patch('sys.stderr', stderr):
            code = upload_symbols.main(list(argv), opener=opener or self.opener(),
                                       sleep=(sleeps.append if sleeps is not None else lambda _: None))
        return code, stdout.getvalue(), stderr.getvalue()

    def test_uploads_each_binary_with_the_token(self):
        code, out, _ = self.run_main('--version', '0.11.8.1', '--origin', 'https://glob2.test/',
                                     str(self.binary('glob2.debug')))
        self.assertEqual(code, 0, out)
        url, headers, body = self.requests[0]
        self.assertEqual(url, 'https://glob2.test/api/v1/crash-symbols?kind=breakpad&version=0.11.8.1')
        self.assertEqual(headers['Authorization'], 'Bearer secret')
        self.assertTrue(body.startswith('MODULE Linux x86_64 ' + 'A' * 32 + '0 glob2\n'))
        self.assertIn('uploaded glob2 ', out)

    def test_names_modules_by_the_shipped_file(self):
        self.run_main('--version', '0.11.8.1', str(self.binary('mingw-glob2.exe.debug')))
        self.assertTrue(self.requests[0][2].startswith('MODULE Linux x86_64 ' + 'A' * 32 + '0 mingw-glob2.exe\n'))
        self.run_main('--version', '0.11.8.1', str(self.binary('glob2.debug')))
        self.assertTrue(self.requests[1][2].startswith('MODULE Linux x86_64 ' + 'A' * 32 + '0 glob2\n'))

    def test_dumps_every_slice_of_a_universal_binary_and_every_dsym_file(self):
        fat = struct.pack('>II', 0xCAFEBABE, 2) + struct.pack('>5I', 0x01000007, 3, 0, 0, 0) \
            + struct.pack('>5I', 0x0100000C, 0, 0, 0, 0)
        self.assertEqual(upload_symbols.fat_architectures(self.binary('fat', fat)), ['x86_64', 'arm64'])
        self.assertEqual(upload_symbols.fat_architectures(self.binary('thin', b'\xcf\xfa\xed\xfe\0\0\0\0')), [])
        dwarf = self.work / 'Glob2.app.dSYM' / 'Contents' / 'Resources' / 'DWARF'
        dwarf.mkdir(parents=True)
        (dwarf / 'Glob2').write_bytes(fat)
        code, out, err = self.run_main('--version', '0.11.8.1', '--dry-run', str(dwarf.parents[2]))
        self.assertEqual(code, 0, err)
        self.assertIn('MODULE Linux x86_64', out)
        self.assertIn('MODULE Linux arm64', out)
        self.assertEqual(self.requests, [])

    def test_refuses_stripped_binaries_and_missing_tokens(self):
        code, _, err = self.run_main('--version', '0.11.8.1', str(self.binary('glob2-stripped')))
        self.assertEqual(code, 1)
        self.assertIn('no debug information', err)
        with mock.patch.dict(os.environ, {'CRASH_SYMBOL_UPLOAD_TOKEN': ''}):
            code, _, err = self.run_main('--version', '0.11.8.1', str(self.binary('glob2.debug')))
        self.assertEqual(code, 1)
        self.assertIn('CRASH_SYMBOL_UPLOAD_TOKEN', err)
        with self.assertRaises(SystemExit):
            self.run_main('--version', '0.11.8-dev', str(self.binary('glob2.debug')))

    def test_retries_busy_servers_but_not_refusals(self):
        sleeps = []
        code, _, _ = self.run_main('--version', '0.11.8.1', str(self.binary('glob2.debug')),
                                   opener=self.opener(503, 429), sleeps=sleeps)
        self.assertEqual(code, 0)
        self.assertEqual((len(self.requests), sleeps), (3, [2, 4]))
        self.requests.clear()
        code, _, err = self.run_main('--version', '0.11.8.1', str(self.binary('glob2.debug')),
                                     opener=self.opener(401))
        self.assertEqual((code, len(self.requests)), (1, 1))
        self.assertIn('refused (401)', err)

    def test_uploads_a_wasm_symbol_map_under_its_build_id(self):
        wasm = self.binary('glob2.wasm', b'\0asm\1\0\0\0')
        symbols = self.work / 'glob2.js.symbols'
        symbols.write_text('0:abort\n1:main\n')
        code, _, err = self.run_main('--version', '0.11.8.1', '--wasm', str(wasm), '--symbol-map', str(symbols))
        self.assertEqual(code, 0, err)
        build_id = hashlib.sha256(wasm.read_bytes()).hexdigest()[:32]
        self.assertIn(f'kind=wasm&version=0.11.8.1&buildId={build_id}', self.requests[0][0])
        self.assertEqual(self.requests[0][2], '0:abort\n1:main\n')
        symbols.write_text('not a map\n')
        self.assertEqual(self.run_main('--version', '0.11.8.1', '--wasm', str(wasm), '--symbol-map',
                                       str(symbols))[0], 1)

    def test_scans_downloaded_artifacts(self):
        artifacts = self.work / 'artifacts'
        (artifacts / 'crash-symbols-release-linux').mkdir(parents=True)
        (artifacts / 'crash-symbols-release-linux' / 'glob2.debug').write_bytes(b'\x7fELF')
        dwarf = artifacts / 'crash-symbols-release-macos-arm64' / 'glob2.dSYM' / 'Contents' / 'Resources' / 'DWARF'
        dwarf.mkdir(parents=True)
        (dwarf / 'glob2').write_bytes(b'\xcf\xfa\xed\xfe')
        (artifacts / 'crash-symbols-play' / 'arm64-v8a').mkdir(parents=True)
        (artifacts / 'crash-symbols-play' / 'arm64-v8a' / 'libmain.so').write_bytes(b'\x7fELF')
        threaded = artifacts / 'crash-symbols-browser' / 'threaded'
        threaded.mkdir(parents=True)
        (threaded / 'index.wasm').write_bytes(b'\0asm')
        (threaded / 'index.js.symbols').write_text('0:main\n')
        (artifacts / 'crash-symbols-browser' / 'index.data').write_bytes(b'ignored')
        code, out, err = self.run_main('--version', '0.11.8.1', '--scan', str(artifacts))
        self.assertEqual(code, 0, err)
        self.assertEqual(len(self.requests), 4)
        modules = sorted(body.split('\n', 1)[0].rsplit(' ', 1)[1] for _, _, body in self.requests if body.startswith('MODULE'))
        self.assertEqual(modules, ['glob2', 'glob2', 'libmain.so'])
        (threaded / 'index.js.symbols').unlink()
        self.assertEqual(self.run_main('--version', '0.11.8.1', '--scan', str(artifacts))[0], 1)
        empty = self.work / 'empty'
        empty.mkdir()
        code, out, _ = self.run_main('--version', '0.11.8.1', '--scan', str(empty))
        self.assertEqual(code, 0)
        self.assertIn('predates', out)

    def test_every_pinned_platform_has_a_checksum(self):
        lock = json.loads(upload_symbols.LOCK.read_text())
        for key in ('Linux-x86_64', 'Linux-aarch64', 'Darwin-x86_64', 'Darwin-arm64', 'Windows-AMD64'):
            self.assertRegex(lock[key]['sha256'], r'^[0-9a-f]{64}$')
            self.assertIn(f'/v{lock["version"]}/', lock[key]['url'])
        with mock.patch('platform.system', return_value='MINGW64_NT-10.0'), \
                mock.patch('platform.machine', return_value='x86_64'):
            self.assertEqual(upload_symbols.host_key(), 'Windows-AMD64')
        with mock.patch('platform.system', return_value='Darwin'), \
                mock.patch('platform.machine', return_value='arm64'):
            self.assertEqual(upload_symbols.host_key(), 'Darwin-arm64')


if __name__ == '__main__':
    unittest.main()
