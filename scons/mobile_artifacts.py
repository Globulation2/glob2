"""Validate Android ELF machines before accepting cross-compiled dependencies."""
from pathlib import Path
import struct

MACHINES={'arm64-v8a':(183,2),'armeabi-v7a':(40,1),'x86_64':(62,2)}


def verify_android_shared_library(path, architecture):
    """Check packaged ELF segments, including Android's 16 KB loader contract."""
    path=Path(path)
    data=path.read_bytes()
    check_header(data,architecture,str(path))
    is64=data[4]==2
    header_size, entry_size=(64,56) if is64 else (52,32)
    if len(data)<header_size: raise ValueError(f'{path}: truncated ELF header')
    if struct.unpack_from('<H',data,16)[0]!=3:
        raise ValueError(f'{path}: expected an ELF shared object')
    offset=struct.unpack_from('<Q' if is64 else '<I',data,32 if is64 else 28)[0]
    stride,count=struct.unpack_from('<HH',data,54 if is64 else 42)
    if stride!=entry_size or not count or offset+stride*count>len(data):
        raise ValueError(f'{path}: invalid ELF program header table')
    loads=0
    for index in range(count):
        position=offset+stride*index
        if struct.unpack_from('<I',data,position)[0]!=1: continue
        loads+=1
        if is64:
            file_offset,address,_,file_size,memory_size,alignment=struct.unpack_from('<6Q',data,position+8)
        else:
            file_offset,address,_,file_size,memory_size,_,alignment=struct.unpack_from('<7I',data,position+4)
        if file_offset+file_size>len(data) or memory_size<file_size:
            raise ValueError(f'{path}: truncated or invalid ELF load segment')
        minimum=16384 if is64 else 4096
        if alignment<minimum or alignment&(alignment-1) or (address-file_offset)%alignment:
            raise ValueError(f'{path}: load segment is not {minimum}-byte page aligned')
    if not loads: raise ValueError(f'{path}: no ELF load segments')


def check_header(header, architecture, label):
    if len(header)<20 or header[:4]!=b'\x7fELF' or header[5]!=1:
        raise ValueError(f'{label}: expected a little-endian Android ELF object')
    machine=struct.unpack_from('<H',header,18)[0]
    expected, elf_class=MACHINES[architecture]
    if machine!=expected or header[4]!=elf_class:
        raise ValueError(f'{label}: wrong architecture (ELF machine {machine}, class {header[4]}); expected {architecture}')


def verify_android_library(path, architecture):
    path=Path(path)
    with path.open('rb') as source:
        magic=source.read(8)
        if magic[:4]==b'\x7fELF':
            source.seek(0);check_header(source.read(20),architecture,str(path));return
        if magic!=b'!<arch>\n': raise ValueError(f'{path}: expected an ELF shared library or regular ar archive')
        objects=0
        while header:=source.read(60):
            if len(header)!=60 or header[58:]!=b'`\n': raise ValueError(f'{path}: corrupt archive header')
            try: size=int(header[48:58])
            except ValueError: raise ValueError(f'{path}: corrupt archive member size') from None
            if size<0: raise ValueError(f'{path}: negative archive member size')
            start=source.tell()
            if start+size>path.stat().st_size: raise ValueError(f'{path}: truncated archive member')
            name=header[:16].decode('ascii').strip()
            if name.startswith('#1/'):
                name_length=int(name[3:])
                if name_length<0 or name_length>size: raise ValueError(f'{path}: invalid archive name length')
                name=source.read(name_length).rstrip(b'\0').decode('utf-8')
            if name not in ('/','//','/SYM64/') and not name.startswith('__.SYMDEF'):
                check_header(source.read(min(20,size)),architecture,f'{path}({name})');objects+=1
            source.seek(start+size+(size%2))
        if not objects: raise ValueError(f'{path}: archive contains no ELF objects')


def archive_object_name(source):
    """Archive members need unique basenames for complete Apple debug maps."""
    import hashlib
    path = Path(source)
    if path.is_absolute() or '..' in path.parts:
        raise ValueError('Archive source must be relative to the repository')
    digest = hashlib.sha256(path.as_posix().encode()).hexdigest()[:16]
    return path.with_name(path.name + '_' + digest + '.o')


def verify_android_symbols(apk, library, architecture, readelf):
    """Require the packaged main library and retained symbols to share a build ID."""
    import re
    import subprocess
    import tempfile
    import zipfile
    apk, library = Path(apk), Path(library)
    member = 'lib/' + architecture + '/libmain.so'
    with zipfile.ZipFile(apk) as archive, tempfile.TemporaryDirectory(prefix='symbol-check-', dir=apk.parent) as temporary:
        if archive.namelist().count(member) != 1:
            raise ValueError('APK must contain exactly one main library for ' + architecture)
        packaged = Path(temporary) / 'libmain.so'
        packaged.write_bytes(archive.read(member))
        identifiers = []
        for candidate in (packaged, library):
            notes = subprocess.check_output([str(readelf), '-n', str(candidate)], text=True)
            matches = re.findall(r'Build ID: ([0-9a-fA-F]{40})(?![0-9a-fA-F])', notes)
            if len(matches) != 1:
                raise ValueError(str(candidate) + ': missing or ambiguous SHA-1 ELF build ID')
            identifiers.append(matches[0].lower())
        if identifiers[0] != identifiers[1]:
            raise ValueError('Packaged Android library and retained debug symbols have different build IDs')
        return identifiers[0]
