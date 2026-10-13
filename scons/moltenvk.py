"""Fetch and select hash-pinned Apple Vulkan compute runtime slices."""
import hashlib
import json
from pathlib import Path
import plistlib
import shutil
import subprocess
import tarfile
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
LOCK = ROOT / 'scons/moltenvk.json'


def digest(path):
    # Apple archives can be large; validation must not retain an archive-sized
    # host allocation alongside the unpacked runtime.
    checksum = hashlib.sha256()
    with Path(path).open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            checksum.update(block)
    return checksum.hexdigest()


def select_slice(extracted, platform, environment):
    package = extracted / 'MoltenVK'
    framework = package / 'MoltenVK/dynamic/MoltenVK.xcframework'
    metadata = framework / 'Info.plist'
    entries = plistlib.loads(metadata.read_bytes())['AvailableLibraries']
    for entry in entries:
        variant = 'simulator' if environment == 'simulator' else None
        if entry['SupportedPlatform'] == ('ios' if platform == 'ios' else 'macos') and entry.get('SupportedPlatformVariant') == variant:
            selected = framework / entry['LibraryIdentifier'] / entry['LibraryPath']
            binary = selected / 'MoltenVK'
            license_path = package / 'LICENSE'
            if not selected.is_dir() or not binary.is_file() or not license_path.is_file():
                raise ValueError('Pinned MoltenVK package has an incomplete framework or licence')
            # Bind the selected runtime and its slice metadata to the archive.
            # Header/debug payloads are not loaded or shipped by this adapter.
            receipts = {str(path.relative_to(extracted)): digest(path)
                        for path in (metadata, binary, license_path)}
            return selected, license_path, receipts
    raise ValueError('Pinned MoltenVK package has no ' + platform + '/' + environment + ' slice')


def validate_package(extracted, platform, environment, archive_hash):
    selected, license_path, receipts = select_slice(extracted, platform, environment)
    receipt = json.loads((extracted / 'runtime-receipt.json').read_text())
    if receipt != {'archive': archive_hash, 'files': receipts}:
        raise ValueError('MoltenVK extracted runtime differs from its verified archive; remove the damaged dependency cache')
    return selected, license_path


def prepare(platform, output, environment='device'):
    if platform not in ('macos', 'ios') or environment not in ('device', 'simulator') or (platform == 'macos' and environment != 'device'):
        raise ValueError('Unsupported MoltenVK platform/environment: ' + platform + '/' + environment)
    lock = json.loads(LOCK.read_text())
    entry = lock['platforms']['ios-simulator' if platform == 'ios' and environment == 'simulator' else platform]
    directory = Path(output) / ('moltenvk-' + lock['version'] + '-' + platform + '-' + environment)
    archive = directory / 'runtime.tar'
    directory.mkdir(parents=True, exist_ok=True)
    if not archive.exists():
        # Publish only a complete verified download. Separate temporary names
        # keep interrupted or concurrent packaging attempts out of the cache.
        with tempfile.TemporaryDirectory(dir=directory, prefix='download-') as staging:
            temporary = Path(staging) / 'runtime.tar'
            urllib.request.urlretrieve(entry['url'], temporary)
            if digest(temporary) != entry['sha256']:
                raise ValueError('MoltenVK archive hash mismatch')
            temporary.replace(archive)
    if digest(archive) != entry['sha256']:
        raise ValueError('MoltenVK archive hash mismatch')
    extracted = directory / 'package-v2'
    if not extracted.exists():
        # Failed extraction must not leave a seemingly reusable package. The
        # verified archive is retained so a later attempt can retry locally.
        with tempfile.TemporaryDirectory(dir=directory, prefix='extract-') as staging:
            candidate = Path(staging) / 'package'
            with tarfile.open(archive) as source:
                source.extractall(candidate, filter='data')
            _, _, receipts = select_slice(candidate, platform, environment)
            (candidate / 'runtime-receipt.json').write_text(json.dumps({'archive': entry['sha256'], 'files': receipts}))
            try:
                candidate.replace(extracted)
            except OSError:
                # Another packager may have published the same complete cache
                # while we extracted. Reuse only after validating its receipts.
                if not extracted.is_dir():
                    raise
                validate_package(extracted, platform, environment, entry['sha256'])
    return validate_package(extracted, platform, environment, entry['sha256'])


def install_macos(bundle, output):
    framework, license = prepare('macos', output)
    bundle = Path(bundle)
    destination_framework = bundle / 'Contents/Frameworks/MoltenVK.framework'
    shutil.copytree(framework, destination_framework, symlinks=True, dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns('Headers', 'Modules'))
    binary = destination_framework / 'Versions/A/MoltenVK'
    executable = bundle / 'Contents/MacOS/glob2'
    architectures = subprocess.check_output(['lipo', '-archs', str(executable)], text=True).split()
    temporary = binary.with_suffix('.thin')
    command = ['lipo', str(binary)]
    for architecture in architectures:
        command.extend(['-extract', architecture])
    subprocess.run([*command, '-output', str(temporary)], check=True)
    temporary.replace(binary)
    subprocess.run(['codesign', '--force', '--sign', '-', str(destination_framework)], check=True)
    destination = bundle / 'Contents/Resources/licenses/MoltenVK'
    destination.mkdir(parents=True, exist_ok=True)
    shutil.copy2(license, destination / 'LICENSE')
