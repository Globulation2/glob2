"""Checksum-pinned, cached lean SDL_image for Mac release packages."""
import hashlib
import json
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile
from dev_store import cache, Lease, hold
from tool_archives import download, digest

ARTIFACT = {
    'url': 'https://github.com/libsdl-org/SDL_image/releases/download/release-2.8.12/SDL2_image-2.8.12.tar.gz',
    'sha256': '393f5efb50536ec13ca4f4affb69cc9966d3c3f969e6c5e701faddf9f9785381',
}
CODECS = ('AVIF','BMP','GIF','JPG','JXL','LBM','PCX','PNG','PNM','QOI','SVG','TGA','TIF','WEBP','XCF','XPM','XV')


def lean_options():
    return ['-DSDL2IMAGE_'+name+'='+('ON' if name in ('PNG','JPG','WEBP') else 'OFF') for name in CODECS] + [
        '-DBUILD_SHARED_LIBS=ON', '-DSDL2IMAGE_DEPS_SHARED=OFF', '-DSDL2IMAGE_STRICT=ON',
        '-DSDL2IMAGE_BACKEND_STB=OFF', '-DSDL2IMAGE_BACKEND_IMAGEIO=OFF',
        '-DSDL2IMAGE_SAMPLES=OFF', '-DSDL2IMAGE_TESTS=OFF', '-DCMAKE_BUILD_TYPE=Release',
        '-DSDL2IMAGE_PNG_SAVE=ON', '-DSDL2IMAGE_JPG_SAVE=ON']


def verified(prefix, identity):
    marker=prefix/'manifest.json'
    if not marker.is_file(): return False
    try:
        record=json.loads(marker.read_text())
        return record['identity']==identity and bool(record['files']) and all(
            (prefix/name).is_file() and digest(prefix/name)==value for name,value in record['files'].items())
    except (ValueError,KeyError,OSError): return False


def ensure(root, jobs=2):
    brew=Path(subprocess.check_output(['brew','--prefix'],text=True).strip())
    identity=dict(archive=ARTIFACT, options=lean_options(), arch=platform.machine(),
        compiler=subprocess.check_output(['clang','--version'],text=True),
        sdk=subprocess.check_output(['xcrun','--show-sdk-version'],text=True),
        dependencies={})
    for package in ('SDL2','libpng','libjpeg','libwebp','libwebpdemux'):
        version=subprocess.check_output(['pkg-config','--modversion',package],text=True).strip()
        libdir=Path(subprocess.check_output(['pkg-config','--variable=libdir',package],text=True).strip())
        identity['dependencies'][package]=dict(version=version, files={p.name:digest(p) for p in sorted(libdir.glob('*.dylib')) if p.is_file()})
    key=hashlib.sha256(json.dumps(identity,sort_keys=True).encode()).hexdigest()[:24]
    location=cache(root,'mac-sdl-image-'+key,lease=False)
    prefix=location/'prefix'
    with Lease(location,exclusive=True):
        if not verified(prefix,identity):
            location.mkdir(parents=True,exist_ok=True)
            archive=download(ARTIFACT,location/'downloads')
            with tempfile.TemporaryDirectory(prefix='build-',dir=location) as temporary:
                task=Path(temporary)
                import tarfile
                with tarfile.open(archive) as source:source.extractall(task,filter='data')
                source=task/'SDL2_image-2.8.12'; build=task/'build'; staged=task/'prefix'
                subprocess.run(['cmake','-S',str(source),'-B',str(build),
                    '-DCMAKE_PREFIX_PATH='+str(brew),'-DCMAKE_INSTALL_PREFIX='+str(prefix),
                    '-DCMAKE_INSTALL_NAME_DIR='+str(prefix/'lib'),*lean_options()],check=True)
                subprocess.run(['cmake','--build',str(build),'--parallel',str(jobs)],check=True)
                subprocess.run(['cmake','--install',str(build),'--prefix',str(staged)],check=True)
                record=dict(identity=identity,files={p.relative_to(staged).as_posix():digest(p) for p in sorted(staged.rglob('*')) if p.is_file()})
                (staged/'manifest.json').write_text(json.dumps(record,indent=2,sort_keys=True)+'\n')
                if prefix.exists():shutil.rmtree(prefix)
                staged.rename(prefix)
        if not verified(prefix,identity):raise RuntimeError('Lean SDL_image verification failed')
    hold(location)
    return prefix
