"""Shared asset export and SCons installation graph for release installs."""
import gzip
import hashlib
import json
from pathlib import Path
import shutil
import sys

STAMP = '.runtime-assets.json.gz'


def install_export(exported, destination):
    """Merge owned runtime files while preserving unrelated installed content."""
    exported, destination = Path(exported), Path(destination)
    audit = json.loads(exported.with_suffix('.json').read_text())
    current = {item['output']: item['output_sha256'] for item in audit['files']}
    marker = destination / STAMP
    previous = {}
    if marker.is_file():
        record = json.loads(gzip.decompress(marker.read_bytes()))
        if record['policy'] != 'runtime-assets-v1':
            raise ValueError('Unknown installed asset policy')
        previous = record['files']
    obsolete = set(previous) - set(current)
    # Upgrade pre-export installations where original PNGs would shadow WebP.
    for item in audit['files']:
        if item['source'] != item['output']:
            old = destination / item['source']
            if old.is_file() and hashlib.sha256(old.read_bytes()).hexdigest() == item['source_sha256']:
                obsolete.add(item['source'])
    for relative in obsolete:
        path = destination / relative
        if not path.resolve().is_relative_to(destination.resolve()):
            raise ValueError('Invalid installed asset path: ' + relative)
        if path.is_file() and (relative not in previous or hashlib.sha256(path.read_bytes()).hexdigest() == previous[relative]):
            path.unlink()
    for relative in ('data/highres/v1/manifest.json', 'data/highres/v1/README.md'):
        (destination / relative).unlink(missing_ok=True)
    for relative in current:
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(exported / relative, target)
    marker.parent.mkdir(parents=True, exist_ok=True)
    record = dict(policy=audit['policy'], files=current)
    marker.write_bytes(gzip.compress(json.dumps(record, sort_keys=True).encode(), mtime=0))


def install_assets(env):
    from SCons.Script import Action, Value
    root = Path.cwd()
    sys.path.insert(0, str(root))
    from tools.package_assets import export_assets, source_files
    platform = 'macos' if sys.platform == 'darwin' else 'windows' if sys.platform == 'win32' else 'linux'
    exported = Path(env['BUILDDIR']).resolve() / 'runtime-assets'
    inputs = [str(p) for p in source_files(root, platform)]

    def export(target, source, env):
        export_assets(root, exported, platform=platform)
        return 0

    stamp = env.Command(str(exported.with_suffix('.json')), inputs + [
        'tools/package_assets.py', 'tools/asset-requirements.txt', 'scons/runtime_assets.py', Value(inputs)],
        Action(export, 'Exporting verified runtime assets'))
    env.Precious(stamp)  # SCons must not unlink the previous ownership audit.
    if not (exported / 'data').is_dir():
        env.AlwaysBuild(stamp)
    destination = Path(env['INSTALLDIR']) / 'glob2'

    def install(target, source, env):
        install_export(exported, destination)
        return 0

    installed = env.Command(str(destination / STAMP), stamp, Action(install, 'Installing verified runtime assets'))
    env.Precious(installed)  # Preserve the inventory used to remove obsolete assets.
    env.AlwaysBuild(installed)
    env.Alias('install', installed)
