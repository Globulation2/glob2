#!/usr/bin/env python3
"""Uploads a release's crash symbols to glob2-online (docs/multiplayer/crash-reports.md#symbols).

Every release workflow runs this before publishing, so every crash from a
shipped build can be read:

    upload_symbols.py --version 0.11.8.1 build/.../src/glob2.debug
    upload_symbols.py --version 0.11.8.1 Glob2.app.dSYM
    upload_symbols.py --version 0.11.8.1 --wasm glob2.wasm --symbol-map glob2.js.symbols
    upload_symbols.py --version 0.11.8.1 --scan downloaded-artifacts/

Native files (an unstripped ELF, PE or Mach-O binary, or a .dSYM bundle) go
through Mozilla's dump_syms (pinned in dump_syms.json, checksum-verified) into
Breakpad .sym files; each universal Mach-O slice becomes its own file. A
WebAssembly build uploads its Emscripten symbol map under the build ID the
browser client reports: the first 32 hex digits of the .wasm file's SHA-256.

CRASH_SYMBOL_UPLOAD_TOKEN (a GitHub Actions secret) authorizes the upload. A
missing token, a failed dump or a refused upload exits non-zero, so the
release job fails rather than shipping a build whose crashes cannot be read.
--dry-run writes nothing anywhere and only prints what would be uploaded.

--scan DIR finds everything above in a directory of downloaded build artifacts
(.github/workflows/crash-symbols.yml): *.debug files, *.dSYM bundles, libmain.so
files and each *.wasm with its *.js.symbols beside it. Finding nothing is not an
error there: builds of tags cut before crash symbols hand over none.
"""

import argparse
import gzip
import hashlib
import json
import os
import platform
import re
import struct
import subprocess
import sys
import tarfile
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'buildsys'))
from build_layout import PACKAGE_VERSION  # noqa: E402
import official_instance  # noqa: E402
from tool_archives import download  # noqa: E402

LOCK = Path(__file__).with_name('dump_syms.json')
TOKEN_ENV = 'CRASH_SYMBOL_UPLOAD_TOKEN'
RELEASE = re.compile(r'^[0-9]+(\.[0-9]+){2,3}$')
MODULE = re.compile(r'^MODULE (\S+) (\S+) ([0-9A-F]{32,40}) ?(.*)$')
CODE_ID = re.compile(rb'^INFO CODE_ID \S+ (\S.*?)\r?$', re.M)
# Mach-O universal headers (big-endian) and the CPU types we ship.
FAT_MAGICS = {0xCAFEBABE: 20, 0xCAFEBABF: 32}
CPU_TYPES = {0x01000007: 'x86_64', 0x0100000C: 'arm64', 7: 'i386', 12: 'arm'}
ATTEMPTS = 4


class UploadError(Exception):
    pass


def host_key():
    system = platform.system()
    if system.startswith(('MSYS', 'MINGW', 'CYGWIN')):
        system = 'Windows'
    machine = platform.machine()
    machine = {'amd64': 'AMD64', 'x86_64': 'AMD64' if system == 'Windows' else 'x86_64',
               'arm64': 'arm64' if system == 'Darwin' else 'aarch64',
               'aarch64': 'arm64' if system == 'Darwin' else 'aarch64'}.get(machine.lower(), machine)
    return f'{system}-{machine}'


def tool_cache():
    explicit = os.environ.get('GLOB2_TOOL_CACHE')
    if explicit:
        return Path(explicit)
    return Path(os.environ.get('XDG_CACHE_HOME', Path.home() / '.cache')) / 'glob2' / 'dump_syms'


def dump_syms():
    """The pinned dump_syms for this machine, downloaded and verified on first use."""
    override = os.environ.get('GLOB2_DUMP_SYMS')
    if override:
        return Path(override)
    lock = json.loads(LOCK.read_text())
    artifact = lock.get(host_key())
    if not artifact:
        raise UploadError(f'dump_syms {lock["version"]} has no build for {host_key()}')
    cache = tool_cache() / lock['version'] / host_key()
    binary = cache / artifact['binary']
    if binary.is_file():
        return binary
    archive = download(artifact, cache / 'downloads')
    with tempfile.TemporaryDirectory(dir=cache, prefix='.extract-') as staging:
        if archive.suffix == '.zip':
            with zipfile.ZipFile(archive) as source:
                source.extractall(staging)
        else:
            with tarfile.open(archive) as source:
                source.extractall(staging, filter='data')
        extracted = Path(staging) / artifact['binary']
        extracted.chmod(0o755)
        binary.parent.mkdir(parents=True, exist_ok=True)
        extracted.replace(binary)
    return binary


def fat_architectures(path):
    """The slices of a universal Mach-O file, or [] for any other file."""
    with open(path, 'rb') as source:
        header = source.read(8)
        if len(header) < 8:
            return []
        magic, count = struct.unpack('>II', header)
        size = FAT_MAGICS.get(magic)
        if not size or count > 32:
            return []
        architectures = []
        for _ in range(count):
            entry = source.read(size)
            if len(entry) < size:
                return []
            cpu = struct.unpack('>I', entry[:4])[0]
            if cpu in CPU_TYPES:
                architectures.append(CPU_TYPES[cpu])
        return architectures


def native_inputs(path):
    """The files dump_syms reads for `path`: a binary, or each DWARF file of a .dSYM."""
    path = Path(path)
    if path.is_dir():
        dwarf = path / 'Contents' / 'Resources' / 'DWARF'
        files = sorted(p for p in dwarf.iterdir() if p.is_file()) if dwarf.is_dir() else []
        if not files:
            raise UploadError(f'{path} is not a .dSYM bundle with DWARF files')
        return files
    if not path.is_file():
        raise UploadError(f'{path} does not exist')
    return [path]


def run_dump_syms(tool, path, arch=None):
    with tempfile.TemporaryDirectory() as work:
        output = Path(work) / 'out.sym'
        command = [str(tool), str(path), '--output', str(output)]
        if arch:
            command += ['--arch', arch]
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode != 0 or not output.is_file():
            raise UploadError(f'dump_syms failed for {path}: {result.stderr.strip()[-500:]}')
        data = output.read_bytes()
    first = data.split(b'\n', 1)[0].decode('latin-1').rstrip('\r')
    match = MODULE.match(first)
    if not match:
        raise UploadError(f'dump_syms wrote no MODULE line for {path}')
    name = match.group(4)
    if not name:
        # MinGW binaries name no PDB; minidumps then name the module by its
        # code file, which is where minidump-stackwalk looks it up.
        code = CODE_ID.search(data)
        if not code:
            raise UploadError(f'{path} names neither a debug file nor a code file')
        name = code.group(1).decode('latin-1')
    # Release builds keep the unstripped client as glob2.debug (glob2.exe.debug);
    # crashes name the shipped file.
    name = name.removesuffix('.debug')
    if name != match.group(4):
        first = f'MODULE {match.group(1)} {match.group(2)} {match.group(3)} {name}'
        data = first.encode('latin-1') + data[data.index(b'\n'):]
    if b'\nFUNC ' not in data:
        # Without FUNC records the build was stripped or compiled without -g1.
        raise UploadError(f'{path} has no debug information (FUNC records)')
    return first, data


def symbol_files(paths):
    """(MODULE line, .sym bytes) for every native input."""
    tool = dump_syms()
    for path in paths:
        for file in native_inputs(path):
            for arch in fat_architectures(file) or [None]:
                yield run_dump_syms(tool, file, arch)


def scan(directory):
    """(native files, [(wasm, symbol map)]) found under a directory of artifacts."""
    root = Path(directory)
    native, wasm = [], []
    for path in sorted(root.rglob('*')):
        if any(parent.suffix == '.dSYM' for parent in path.parents if parent != root and root in parent.parents):
            continue  # inside a bundle already taken whole
        if path.is_dir() and path.suffix == '.dSYM':
            native.append(path)
        elif path.is_file() and (path.suffix == '.debug' or path.name == 'libmain.so'):
            native.append(path)
        elif path.is_file() and path.suffix == '.wasm':
            symbols = path.with_name(path.stem + '.js.symbols')
            if not symbols.is_file():
                raise UploadError(f'{path} has no {symbols.name} beside it')
            wasm.append((path, symbols))
    return native, wasm


def build_id(wasm):
    digest = hashlib.sha256()
    with open(wasm, 'rb') as source:
        while block := source.read(1 << 20):
            digest.update(block)
    return digest.hexdigest()[:32]


def check_symbol_map(path):
    with open(path, 'rb') as source:
        first = source.readline().decode('utf-8', 'replace')
    if not re.match(r'^[0-9]+:\S', first):
        raise UploadError(f'{path} is not an Emscripten symbol map (index:name lines)')
    return Path(path).read_bytes()


def post(origin, token, query, body, opener=urllib.request.urlopen, sleep=time.sleep):
    url = f'{origin}/api/v1/crash-symbols?{urllib.parse.urlencode(query)}'
    for attempt in range(ATTEMPTS):
        request = urllib.request.Request(url, data=body, method='POST', headers={
            'content-type': 'application/gzip',
            'authorization': f'Bearer {token}',
            'user-agent': 'glob2-release-symbols',
        })
        try:
            with opener(request, timeout=300) as response:
                return json.loads(response.read().decode())
        except urllib.error.HTTPError as error:
            detail = error.read().decode('utf-8', 'replace')[:300]
            retry = error.code in (408, 429) or error.code >= 500
            if not retry or attempt == ATTEMPTS - 1:
                raise UploadError(f'upload refused ({error.code}): {detail}') from error
        except (urllib.error.URLError, OSError) as error:
            if attempt == ATTEMPTS - 1:
                raise UploadError(f'upload failed: {error}') from error
        sleep(2 ** (attempt + 1))
    raise AssertionError('unreachable')


def main(argv=None, opener=urllib.request.urlopen, sleep=time.sleep):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('files', nargs='*', help='unstripped binaries or .dSYM bundles')
    parser.add_argument('--version', default=PACKAGE_VERSION, help='release version (default: PACKAGE_VERSION)')
    parser.add_argument('--origin', default=os.environ.get('GLOB2_CRASH_SYMBOL_ORIGIN', official_instance.DEFAULT_ORIGIN))
    parser.add_argument('--wasm', help='the shipped .wasm file, for its build ID')
    parser.add_argument('--symbol-map', help="the build's Emscripten symbol map (emcc --emit-symbol-map)")
    parser.add_argument('--scan', help='a directory of build artifacts to find symbols in')
    parser.add_argument('--dry-run', action='store_true', help='dump and check, but upload nothing')
    args = parser.parse_args(argv)
    if not RELEASE.match(args.version):
        parser.error(f'{args.version} is not a release version')
    if bool(args.wasm) != bool(args.symbol_map):
        parser.error('--wasm and --symbol-map go together')
    if not args.files and not args.wasm and not args.scan:
        parser.error('nothing to upload')
    origin = args.origin.rstrip('/')
    token = os.environ.get(TOKEN_ENV, '')
    if not token and not args.dry_run:
        print(f'upload_symbols: {TOKEN_ENV} is not set', file=sys.stderr)
        return 1
    try:
        files, wasms = list(args.files), [(args.wasm, args.symbol_map)] if args.wasm else []
        if args.scan:
            found, found_wasm = scan(args.scan)
            if not found and not found_wasm:
                print(f'::notice::No crash symbols in {args.scan}: the build predates them')
                return 0
            files += found
            wasms += found_wasm
        uploads = []
        for module, data in symbol_files(files):
            uploads.append((module, {'kind': 'breakpad', 'version': args.version}, gzip.compress(data, 6)))
        for wasm, symbol_map in wasms:
            identity = build_id(wasm)
            uploads.append((f'glob2.wasm {identity}', {'kind': 'wasm', 'version': args.version,
                                                       'buildId': identity},
                            gzip.compress(check_symbol_map(symbol_map), 6)))
        for label, query, body in uploads:
            if args.dry_run:
                print(f'would upload {label} ({len(body)} bytes)')
                continue
            receipt = post(origin, token, query, body, opener, sleep)
            print(f'uploaded {receipt["module"]} {receipt["debugId"]} for {receipt["version"]}'
                  + (' (replaced)' if receipt.get('replaced') else ''))
    except UploadError as error:
        print(f'upload_symbols: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
