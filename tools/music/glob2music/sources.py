# SPDX-License-Identifier: GPL-3.0-or-later
"""Pinned downloads of third-party inputs: URL + SHA-256, cached outside git.

Pipeline role: sets built from existing recordings or sample libraries declare each
input in ``set.toml`` (``[[sources]]`` with ``name``, ``url``, ``sha256``, licence and
author). Recipes ask ``ctx.source(name)`` for a local path; that calls ``fetch``,
which downloads once into ``tools/music/cache/sources/`` (git-ignored) and verifies
the hash every time. A changed upstream file therefore stops the build instead of
silently changing a shipped set, and builds after the first one need no network.

Only official sources belong here (project sites, GitHub releases, Hugging Face,
OpenGameArt, ...); the licence fields in ``set.toml`` record why each input may be
redistributed under GPL-compatible terms.
"""
from dataclasses import dataclass
import hashlib
import os
from pathlib import Path
import shutil
import tempfile
import urllib.parse
import urllib.request
import zipfile

#: tools/music/cache; override with GLOB2MUSIC_CACHE.
DEFAULT_CACHE = Path(os.environ.get('GLOB2MUSIC_CACHE', Path(__file__).resolve().parents[1] / 'cache'))
USER_AGENT = 'glob2music/1 (+https://github.com/Globulation2/glob2)'


class SourceError(RuntimeError):
    """A source could not be fetched or failed verification."""


@dataclass(frozen=True)
class Source:
    """One pinned input, as declared in ``set.toml``."""

    name: str
    url: str
    sha256: str
    license: str = ''
    author: str = ''
    title: str = ''
    filename: str = ''
    note: str = ''

    @property
    def cache_name(self):
        """File name in the cache: the declared ``filename`` or the URL's last part."""
        return self.filename or Path(urllib.parse.urlparse(self.url).path).name or self.name


def sha256_of(path, chunk=1 << 20):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(chunk), b''):
            h.update(block)
    return h.hexdigest()


def verify(path, sha256):
    """Raise ``SourceError`` unless ``path`` hashes to ``sha256``."""
    got = sha256_of(path)
    if got.lower() != sha256.lower():
        raise SourceError(f'{path}: sha256 {got} does not match the pinned {sha256}')
    return path


def fetch(url, sha256, cache_dir=None, filename=None, offline=False, timeout=60):
    """Return a verified local copy of ``url``, downloading it only if needed.

    The file lives at ``<cache_dir>/sources/<sha256[:12]>/<filename>`` so two sources
    with the same name never collide. With ``offline`` (or ``GLOB2MUSIC_OFFLINE=1``)
    a missing file raises instead of touching the network. A ``file://`` URL or a
    plain local path is copied, which lets tests and air-gapped builds use mirrors.
    """
    if not sha256 or len(sha256) != 64:
        raise SourceError(f'{url}: a 64-hex-digit sha256 pin is required')
    cache = Path(cache_dir) if cache_dir else DEFAULT_CACHE
    name = filename or Path(urllib.parse.urlparse(url).path).name or 'download'
    dest = cache / 'sources' / sha256[:12].lower() / name
    if dest.exists():
        return verify(dest, sha256)
    if offline or os.environ.get('GLOB2MUSIC_OFFLINE') == '1':
        raise SourceError(f'{name} is not cached and offline mode is on ({url})')
    dest.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp = tempfile.mkstemp(dir=dest.parent, prefix='.part-')
    try:
        with os.fdopen(fd, 'wb') as out:
            parsed = urllib.parse.urlparse(url)
            if parsed.scheme in ('', 'file'):
                with open(urllib.parse.unquote(parsed.path), 'rb') as src:
                    shutil.copyfileobj(src, out)
            else:
                req = urllib.request.Request(url, headers={'User-Agent': USER_AGENT})
                with urllib.request.urlopen(req, timeout=timeout) as resp:
                    shutil.copyfileobj(resp, out)
        verify(tmp, sha256)
        os.replace(tmp, dest)
    except BaseException:
        Path(tmp).unlink(missing_ok=True)
        raise
    return dest


def fetch_source(source, cache_dir=None, offline=False):
    """``fetch`` for a ``Source`` declared in a manifest."""
    return fetch(source.url, source.sha256, cache_dir=cache_dir, filename=source.cache_name, offline=offline)


def extract(archive, member, cache_dir=None):
    """Extract one member of a zip archive next to it (once) and return its path.

    Archives such as OpenGameArt stem packs are pinned as a whole; recipes pull the
    files they need with this. Member names are checked against path traversal. The
    member is written to a temporary file and renamed into place, so an interrupted
    extraction never leaves a truncated file under the real name.
    """
    archive = Path(archive)
    out_dir = Path(cache_dir) if cache_dir else archive.parent / (archive.stem + '.d')
    dest = (out_dir / member).resolve()
    if out_dir.resolve() not in dest.parents:
        raise SourceError(f'{member}: unsafe archive member path')
    if not dest.exists():
        dest.parent.mkdir(parents=True, exist_ok=True)
        fd, tmp = tempfile.mkstemp(dir=dest.parent, prefix='.part-')
        try:
            with zipfile.ZipFile(archive) as z, z.open(member) as src, os.fdopen(fd, 'wb') as out:
                shutil.copyfileobj(src, out)
            os.replace(tmp, dest)
        except BaseException:
            Path(tmp).unlink(missing_ok=True)
            raise
    return dest
