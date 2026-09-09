#!/usr/bin/env python3
"""Qualify tactical switch fixtures against unchanged, frozen engine objects."""
import argparse
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import maxima_win_experiment as exp


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('campaign', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--cpu', type=int, required=True)
    args = parser.parse_args()
    os.chdir(exp.ROOT)
    protocol = json.loads((args.campaign / 'protocol.json').read_text())
    exp.verify_freeze(protocol)
    if args.cpu not in os.sched_getaffinity(0):
        raise ValueError('CPU must be in the permitted affinity')
    os.sched_setaffinity(0, {args.cpu})
    args.output.mkdir(parents=True, exist_ok=False)
    packages = ['sdl2', 'SDL2_net', 'SDL2_ttf', 'SDL2_image', 'vorbisfile', 'speex', 'fribidi', 'epoxy']
    cflags = shlex.split(subprocess.check_output(['pkg-config', '--cflags', *packages], text=True))
    libs = shlex.split(subprocess.check_output(['pkg-config', '--libs', *packages], text=True))
    sources = (exp.ROOT / 'src/SConscript').read_text().split('"""')[1].split()
    objects = [args.build / 'src' / Path(s).with_suffix('.o') for s in sources if s != 'Glob2.cpp']
    objects += [args.build / 'libgag/src/libgag.a', args.build / 'libusl/src/libusl.a']
    source = exp.ROOT / 'test/MaximaTacticsSwitchBehaviorTest.cpp'
    # FileManager has a short executable-path buffer; run the fixture from /tmp.
    with tempfile.TemporaryDirectory(prefix='mx-', dir='/tmp') as temporary:
        binary = Path(temporary) / 'test'
        command = ['c++', '-std=gnu++20', '-O1', '-UNDEBUG', '-I.', '-Isrc', '-Ilibgag/include',
                   '-Ilibusl/src', *['-I'+str(p) for p in (exp.ROOT/'src').rglob('*') if p.is_dir()],
                   *cflags, str(source), *map(str, objects), *libs, '-lboost_date_time', '-lpthread',
                   '-lz', '-lGL', '-lGLU', '-o', str(binary)]
        exp.atomic(args.output/'command.json', {'argv': command})
        subprocess.run(command, check=True)
        shutil.copy2(binary, args.output/'fixture-binary')
        result = subprocess.run([str(binary)], capture_output=True, text=True, env=exp.clean_environment())
        (args.output/'native.log').write_text(result.stdout+result.stderr)
        result.check_returncode()
    exp.verify_freeze(protocol)
    exp.atomic(args.output/'PASS.json', {
        'protocol_id': protocol['protocol_id'], 'passed': True,
        'validated_switches': ['tactics.dig_out_enabled','tactics.failed_target_quarantine_enabled','tactics.siege_target_lock_enabled'], 'cases': 48,
        'checks': ['paired ON/OFF', 'quarantined target authorization and expiry', 'resource-sealed target route opening authorization and worker availability', 'progress target commitment, expiry and eliminated opponent'],
        'parent_disabled': 'dig-out and quarantine tested with tactics and siege independently disabled; target lock selection tested with no progress, dead opponent and expired commitment',
        'source_sha256': exp.sha(source), 'script_sha256': exp.sha(__file__),
        'command_sha256': exp.sha(args.output/'command.json'),
        'objects': {str(p): exp.sha(p) for p in objects},
        'fixture_binary_sha256': exp.sha(args.output/'fixture-binary'),
        'native_log_sha256': exp.sha(args.output/'native.log'), 'cpu': args.cpu,
        'purpose': 'behavioral qualification only; never inferential samples'})
    print('Three tactics switches: 48 behavioral cases PASS')


if __name__ == '__main__':
    main()
