"""Storage bounds and streaming logs shared by tournament supervisors."""
import gzip
import hashlib
import os
from pathlib import Path
import threading

MIB = 1024**2
GIB = 1024**3
DEFAULT_ATTEMPT_BYTES = 512 * MIB
DEFAULT_LOG_BYTES = 64 * MIB
PROBABILITY_PREFIXES = (b'GLOB2_WINPROB ',)
TELEMETRY_PREFIXES = PROBABILITY_PREFIXES + (b'MAXIMA_TELEMETRY\t', b'GLOB2_TL ',
    b'GLOB2_ECON ', b'GLOB2_TIMELINE ')


def directory_bytes(directory):
    total = 0
    for root, _, files in os.walk(directory):
        for name in files:
            path = Path(root) / name
            try:
                if not path.is_symlink():
                    total += path.stat().st_size
            except FileNotFoundError:
                pass  # Acknowledgement and atomic publication may remove files.
    return total


class CompressedLog:
    """Drain one child pipe without retaining an uncompressed temporary file.

    The cap applies to retained, decoded bytes, bounding even incompressible
    output. On overflow the valid retained prefix is closed, the pipe continues
    draining, and the supervisor reports storage_limit rather than success.
    """
    def __init__(self, source, path, limit, mode='full'):
        if mode not in ('full', 'win-probability', 'telemetry'):
            raise ValueError('invalid log mode')
        self.source, self.path, self.limit, self.mode = source, Path(path), limit, mode
        self.prefixes = PROBABILITY_PREFIXES if mode == 'win-probability' else TELEMETRY_PREFIXES
        self.error = None
        self.raw_bytes = self.received_bytes = 0
        self.digest = hashlib.sha256()
        self.complete = False
        self.thread = threading.Thread(target=self._capture, daemon=True)
        self.thread.start()

    def _capture(self):
        try:
            with self.path.open('wb') as destination:
                with gzip.GzipFile(fileobj=destination, mode='wb', filename='', mtime=0,
                                   compresslevel=1) as encoded:
                    beginning, keep = True, False
                    while True:
                        block = (self.source.readline(64 * 1024) if self.mode != 'full'
                                 else self.source.read(64 * 1024))
                        if not block:
                            break
                        self.received_bytes += len(block)
                        if self.mode != 'full':
                            if beginning:
                                keep = block.startswith(self.prefixes)
                            beginning = block.endswith(b'\n')
                            if not keep:
                                continue
                        if self.error:
                            continue
                        if self.raw_bytes + len(block) > self.limit:
                            self.error = 'retained log exceeds log byte limit'
                            continue
                        encoded.write(block)
                        self.digest.update(block)
                        self.raw_bytes += len(block)
                destination.flush()
                os.fsync(destination.fileno())
            self.complete = True
        except Exception as error:
            self.error = 'log capture failed: ' + str(error)
            # A failed destination must not leave the child blocked on its pipe.
            while True:
                block = self.source.read(64 * 1024)
                if not block or not isinstance(block, bytes):
                    break
        finally:
            self.source.close()

    def finish(self):
        self.thread.join()
        return {'encoding': 'gzip', 'raw_bytes': self.raw_bytes,
                'raw_sha256': self.digest.hexdigest(), 'received_bytes': self.received_bytes,
                'mode': self.mode, 'complete': self.complete, 'error': self.error}
