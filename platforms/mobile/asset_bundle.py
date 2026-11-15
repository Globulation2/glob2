"""Keep the mobile asset index consistent with the files Android packages."""
import hashlib
import gzip
from pathlib import PurePosixPath
import zipfile

# Keep this packaging bound in step with AssetIndex.h's native reader.
MAX_INDEX_BYTES = 4 * 1024 * 1024


def encode_asset_index(identity, names):
    """Reject a bundle the bounded native startup reader cannot install."""
    index = (identity + '\n' + '\n'.join(names) + '\n').encode('utf-8')
    if len(index) > MAX_INDEX_BYTES:
        raise ValueError('Asset index exceeds native startup capacity')
    return index


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


def restore_gzip_assets(package_path, assets, prefix='assets/glob2-bundle/'):
    """Restore gzip files that AAPT expands and renames in an APK or AAB."""
    with zipfile.ZipFile(package_path) as package:
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
        with zipfile.ZipFile(package_path, 'a') as package:
            for source, name in missing:
                package.write(source, name, compress_type=zipfile.ZIP_STORED)
    return bool(missing)


def verify_apk_assets(package_path, prefix='assets/glob2-bundle/'):
    """Verify packaged assets against their identity in an APK or AAB."""
    with zipfile.ZipFile(package_path) as package:
        if package.getinfo(prefix + 'index.list').file_size > MAX_INDEX_BYTES:
            raise ValueError('Asset index exceeds native startup capacity')
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


# Driver family, artifact format and magic the native SDL GPU factory accepts
# per mobile platform (libs/gag/src/SDLGPUDevice.cpp sdlGPUUnavailableReason).
SDL_GPU_SHADERS = {'android': ('vulkan', 'spirv', bytes.fromhex('03022307')),
                   'ios': ('metal', 'metallib', b'MTLB')}


def verify_sdl_gpu_shaders(read, target):
    """Check a package holds every SDL GPU shader its metadata advertises.

    ``read(relative)`` returns the bytes of a file below the packaged shader
    directory, raising KeyError or OSError when it is absent. Returns the
    number of verified shader binaries; raises ValueError naming what is
    missing so a package without SDL GPU never passes as complete.
    """
    import json
    family, format_name, magic = SDL_GPU_SHADERS[target]
    try:
        meta = json.loads(read('metadata.json'))
    except (KeyError, OSError, ValueError) as error:
        raise ValueError('Package lacks readable renderer shader metadata') from error
    variants = meta.get('variants') or []
    artifacts = meta.get('shaderArtifacts', {}).get(family)
    if not variants or not artifacts:
        raise ValueError('Package has no SDL GPU ' + family + ' shaders; the game would report SDL GPU unavailable')
    count = 0
    for variant in variants:
        for stage in ('vertex', 'fragment'):
            receipt = artifacts.get(variant, {}).get(stage)
            if not receipt or receipt.get('format') != format_name:
                raise ValueError('Package lacks SDL GPU ' + family + ' ' + variant + ' ' + stage + ' shader')
            relative = PurePosixPath(receipt['path'])
            if relative.is_absolute() or '..' in relative.parts:
                raise ValueError('Invalid packaged shader path: ' + receipt['path'])
            try:
                contents = read(relative.as_posix())
            except (KeyError, OSError) as error:
                raise ValueError('Package is missing SDL GPU shader ' + receipt['path']) from error
            if not contents.startswith(magic) or hashlib.sha256(contents).hexdigest() != receipt.get('sha256'):
                raise ValueError('Packaged SDL GPU shader does not match its receipt: ' + receipt['path'])
            count += 1
    return count


def verify_apk_sdl_gpu(package_path, prefix='assets/glob2-bundle/'):
    """Verify the Vulkan SPIR-V set inside an APK or AAB."""
    with zipfile.ZipFile(package_path) as package:
        return verify_sdl_gpu_shaders(lambda relative: package.read(prefix + 'shaders/' + relative), 'android')
