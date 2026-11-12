"""A machine-wide CPU token pool shared by every build, test run and asset export.

Several checkouts building at once each start `-j ncpu` compilers, so four lanes
run four times as many processes as there are CPUs: every job slows down and the
machine swaps. The pool caps the processes doing real work across all lanes at
`ncpu + 1`, whatever each lane's `-j`.

Tokens are lock files `<dev home>/state/jobserver/token-NN`. Holding an exclusive
`flock` on one is holding a token; the kernel releases it when the holder exits,
so a crashed or killed process never leaks a token (which is why this is not a
POSIX semaphore or a pipe of bytes).

- `test/run_tests.py` holds one token per running test process.
- `tools/package_assets.py` encoding workers hold one token for their lifetime.

A process started while its parent holds a token inherits `GLOB2_JOBSERVER_HELD`
and acquires nothing, so a token holder never waits on a token its own child
needs. `GLOB2_JOBSERVER=0` disables the pool; `GLOB2_JOBSERVER_TOKENS` overrides
its size. Windows builds run without the pool.
"""
from __future__ import annotations

import contextlib
import os
import random
import time
from pathlib import Path

HELD = 'GLOB2_JOBSERVER_HELD'


def enabled() -> bool:
    return (os.name != 'nt' and os.environ.get('GLOB2_JOBSERVER', '1').lower() not in ('0', 'false', 'no', 'off')
            and not os.environ.get(HELD))


def token_count() -> int:
    configured = os.environ.get('GLOB2_JOBSERVER_TOKENS')
    return max(1, int(configured)) if configured else (os.cpu_count() or 1) + 1


def directory() -> Path:
    import sys
    here = str(Path(__file__).resolve().parent)
    if here not in sys.path:
        sys.path.append(here)
    from dev_store import home
    path = home() / 'state/jobserver'
    path.mkdir(parents=True, exist_ok=True)
    return path


class Token:
    """One held token; release() or process exit gives it back."""

    def __init__(self, handle=None):
        self.handle = handle

    def release(self) -> None:
        if self.handle is not None:
            self.handle.close()  # closing the descriptor drops the flock
            self.handle = None

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.release()


def _try(path: Path):
    import fcntl
    handle = path.open('a+b')
    try:
        fcntl.flock(handle.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        handle.close()
        return None
    return handle


def acquire(count: int | None = None, folder: Path | None = None) -> Token:
    """Wait for a free token; a no-op token when the pool is disabled."""
    if not enabled():
        return Token()
    folder = folder or directory()
    names = [folder / f'token-{index:03d}' for index in range(count or token_count())]
    delay = 0.005
    while True:
        random.shuffle(names)
        for path in names:
            handle = _try(path)
            if handle is not None:
                return Token(handle)
        time.sleep(delay)
        delay = min(delay * 2, 0.1)


@contextlib.contextmanager
def slot():
    """Hold a token for the duration of a block. Start children with child_environment()."""
    with acquire() as token:
        yield token


def child_environment(environment: dict | None = None) -> dict:
    """An environment for a child process of a token holder."""
    result = dict(os.environ if environment is None else environment)
    result[HELD] = '1'
    return result


_lifetime_token = None


def hold_for_process_lifetime() -> None:
    """ProcessPoolExecutor initializer: the worker keeps one token until it exits."""
    global _lifetime_token
    _lifetime_token = acquire()


# Lane registry: which builds are running on this machine (dev_environment.py lanes).
def _lanes() -> Path:
    path = directory().parent / 'lanes'
    path.mkdir(parents=True, exist_ok=True)
    return path


def register_lane(worktree, builddir, argv) -> None:
    """Record this build until it exits; stale records (dead pids) are ignored and removed."""
    import atexit
    import json
    record = _lanes() / f'{os.getpid()}.json'
    record.write_text(json.dumps({'pid': os.getpid(), 'worktree': str(worktree), 'builddir': str(builddir),
                                  'start': time.time(), 'argv': list(argv)}))
    atexit.register(lambda: record.unlink(missing_ok=True))


def _alive(pid: int) -> bool:
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


def live_lanes() -> list[dict]:
    import json
    lanes = []
    for record in sorted(_lanes().glob('*.json')):
        try:
            lane = json.loads(record.read_text())
        except (OSError, ValueError):
            continue
        if _alive(int(lane.get('pid', 0))):
            lanes.append(lane)
        else:
            record.unlink(missing_ok=True)
    return lanes
