"""Keep the mobile asset index consistent with the files Android packages."""
import hashlib
import gzip
from pathlib import PurePosixPath
import zipfile


def include_asset(relative):
    """Exclude local caches/metadata that AAPT omits from packaged assets.

    These are build inputs, not game content. Filtering before indexing is
    essential: the native installer treats every indexed path as required.
    """
    path = PurePosixPath(relative)
    return (not any(part.startswith('.') or part == 'CVS' or
                    part == '__pycache__' for part in path.parts)
            and path.suffix not in ('.pyc', '.pyo', '.scc')
            and not path.name.endswith('~'))


def restore_gzip_assets(apk, assets):
    """Restore gzip files that AAPT expands and renames while packaging assets."""
    prefix = 'assets/glob2-bundle/'
    with zipfile.ZipFile(apk) as package:
        packaged = set(package.namelist())
        missing = []
        for source in sorted(assets.rglob('*.gz')):
            relative = source.relative_to(assets).as_posix()
            name = prefix + relative
            if name in packaged:
                continue
            expanded = name[:-3]
            if expanded not in packaged or package.read(expanded) != gzip.decompress(source.read_bytes()):
                raise ValueError('AAPT did not package gzip asset as expected: ' + relative)
            missing.append((source, name))
    if missing:
        with zipfile.ZipFile(apk, 'a') as package:
            for source, name in missing:
                package.write(source, name, compress_type=zipfile.ZIP_STORED)
    return bool(missing)


def verify_apk_assets(apk):
    """Verify the final APK, after AAPT filtering, against its asset identity."""
    prefix = 'assets/glob2-bundle/'
    with zipfile.ZipFile(apk) as package:
        lines = package.read(prefix + 'index.list').decode('utf-8').splitlines()
        if not lines or len(lines[0]) != 64:
            raise ValueError('Invalid packaged asset index')
        digest = hashlib.sha256()
        seen = set()
        for name in lines[1:]:
            path = PurePosixPath(name)
            if not name or path.is_absolute() or '..' in path.parts or name in seen:
                raise ValueError('Invalid or duplicate indexed asset: ' + name)
            seen.add(name)
            try:
                contents = package.read(prefix + name)
            except KeyError as error:
                raise ValueError('APK is missing indexed asset: ' + name) from error
            digest.update(name.encode() + b'\0' + hashlib.sha256(contents).digest())
        if digest.hexdigest() != lines[0]:
            raise ValueError('Packaged assets do not match their indexed identity')
